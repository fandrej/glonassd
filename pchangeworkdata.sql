-- gps.pchangeworkdata — суточная смена рабочей даты (таймер glonassd 04:00, pg_change_work_date.sql):
-- вчерашние точки из gps.tgpsdata → архив gps.tgps<месяц>, очистка рабочих таблиц, удаление устаревших логов.
-- Исходник в базе раньше не хранился в репозитории; сохранён 08.11.26 при исправлении очистки архивов.
-- Применить: psql <locman> -v ON_ERROR_STOP=1 -f pchangeworkdata.sql

CREATE OR REPLACE FUNCTION gps.pchangeworkdata(pnmonthdelete integer)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    dYesterday DATE;
    nDay INTEGER;
    nMonth INTEGER;
    nYear INTEGER;
    nYear1 INTEGER;
    sTable VARCHAR;
    sSQL VARCHAR;
BEGIN
    -- перенос данных за предыдущую дату в архивные таблицы из рабочих
    BEGIN
        SELECT date(timezone('UTC', now()))-1 INTO dYesterday; -- вчера
        -- и производные:
        SELECT extract(DAY FROM dYesterday),    -- число
                extract(MONTH FROM dYesterday), -- месяц
                extract(YEAR FROM dYesterday)   -- год
        INTO nDay, nMonth, nYear;

        sTable := 'gps.tgps' || to_char(nMonth, 'fm99'); -- имя архивной таблицы
        -- в архивной таблице месяца — только этот месяц этого года: данные того же месяца прошлых лет удаляются
        -- (срок хранения ~12 мес.). Раньше: truncate 2-го числа, если в таблице прошлый год, — но GREATEST(год, nYear)
        -- всегда давал nYear, и таблицы не чистились никогда (найдено 08.11.26). Удаление по индексу ttime —
        -- каждый день, поэтому не пропадает, если glonassd не работал 2-го числа.
        EXECUTE 'DELETE FROM ' || sTable || ' WHERE ttime < $1'
        USING date_trunc('month', dYesterday::timestamp) AT TIME ZONE 'UTC';

        sSQL := 'INSERT INTO ' || sTable || '
        (
            dsysdata,cimei,nstatus,naltitude,nspeed,nheading,nsat,nvalid,nnum,
            nvbort,nvbat,ntmp,nhdop,nout,ninp,nin0,nin1,nin2,nin3,nin4,nin5,nin6,nin7,
            nzaj,nalarm,nfuel1,nfuel2,nfuel3,nfuel4,ttime,gpoint,nprobeg,
            textra
            -- TODO: delete
            ,ddata,
            ntime,
            nlongitude,
            nlatitude,
            nprobegc -- бессмысленно рассчитывать здесь, посчитаем при выборке
        )
        SELECT -- выборка не дублирующихся записей
            dsysdata,cimei,nstatus,naltitude,nspeed,nheading,nsat,nvalid,nnum,
            nvbort,nvbat,ntmp,nhdop,nout,ninp,nin0,nin1,nin2,nin3,nin4,nin5,nin6,nin7,
            nzaj,nalarm,nfuel1,nfuel2,nfuel3,nfuel4,ttime,gpoint,nprobeg,
            -- TODO: delete
            cmessage -- > textra
            ,ddata,
            ntime,
            nlongitude,
            nlatitude,
            nprobegc -- бессмысленно рассчитывать здесь, посчитаем при выборке
        FROM (
            SELECT -- выборка всех записей с ранжированием дубликатов по времени
                ROW_NUMBER() OVER(PARTITION BY ttime, cimei ORDER BY ttime) AS RANK,
                dsysdata,cimei,nstatus,naltitude,nspeed,nheading,nsat,nvalid,nnum,
                nvbort,nvbat,ntmp,nhdop,nout,ninp,nin0,nin1,nin2,nin3,nin4,nin5,nin6,nin7,
                nzaj,nalarm,nfuel1,nfuel2,nfuel3,nfuel4,ttime,gpoint,nprobeg, -- присланный трекером пробег
                -- TODO: delete
                cmessage
                ,ddata,
                ntime,
                nlongitude,
                nlatitude,
                nprobegc
            FROM gps.tgpsdata
            -- делать после 0 часов UTC по времени сервера, т.е. после 3:00 по Мск
            WHERE ttime >= date_trunc(''month'', timezone(''utc'', now())) -- first day of month
            AND ttime < TIMESTAMP ''today'' -- yesterday
            -- только зарегистрированные трекеры
            AND EXISTS (SELECT cimei FROM gps.ttrackerkontra WHERE cimei = gps.tgpsdata.cimei)
        ) a
        WHERE RANK = 1';

        EXECUTE sSQL;

        DELETE FROM gps.tgpsdata WHERE ttime::date <> date(timezone('UTC', now())) or ttime is null;
    EXCEPTION
        WHEN OTHERS THEN
            insert into gps.terrorlog (cwhere, csqlerrm)
            values ('gps.pchangeworkdata', SQLSTATE||':'||SQLERRM);
    END;

    BEGIN
        INSERT INTO gps.tkapkaneventa
        SELECT
          a.ddata, a.nkapkan, a.cimei, a.ntime, a.nevent, a.nalarm, a.nalarmed, a.nid
        FROM (
            SELECT t.*, ROW_NUMBER() OVER(PARTITION BY ddata, nkapkan, cimei, ntime ORDER BY ntime) AS RANK
            FROM gps.tkapkanevent t
            WHERE ddata <= dYesterday
            AND EXISTS (SELECT cimei FROM gps.ttrackerkontra WHERE cimei = t.cimei)
        ) a
        WHERE RANK = 1;

        DELETE FROM gps.tkapkanevent WHERE ddata <> dYesterday + 1;
    EXCEPTION
        WHEN OTHERS THEN
            insert into gps.terrorlog (cwhere, csqlerrm)
            values ('gps.pchangeworkdata', SQLSTATE||':'||SQLERRM);
    END;

    -- очистка рабочих таблиц
    BEGIN
        DELETE
        FROM gps.ttrackerlog
        WHERE NOT EXISTS (SELECT cimei FROM gps.ttracker WHERE cimei = gps.ttrackerlog.cimei)
        OR ttime::date > date(timezone('UTC', now()));
    EXCEPTION
        WHEN OTHERS THEN
            insert into gps.terrorlog (cwhere, csqlerrm)
            values ('gps.pchangeworkdata', SQLSTATE||':'||SQLERRM);
    END;

    -- удаление устаревших данных (старше pnmonthdelete месяцев)
    BEGIN
        DELETE FROM gps.tkapkaneventa WHERE gps.months_between(CURRENT_DATE, ddata::date) > pnmonthdelete;
        DELETE FROM gps.tsmslog WHERE gps.months_between(CURRENT_DATE, dsysdata::date) > pnmonthdelete;
        DELETE FROM gps.terrorlog WHERE gps.months_between(CURRENT_DATE, ddata::date) > pnmonthdelete;
    EXCEPTION
        WHEN OTHERS THEN
            insert into gps.terrorlog (cwhere, csqlerrm)
            values ('gps.pchangeworkdata', SQLSTATE||':'||SQLERRM);
    END;

END;
$function$;
