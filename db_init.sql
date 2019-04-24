--
-- PostgreSQL database dump
--

-- Dumped from database version 10.6 (Ubuntu 10.6-0ubuntu0.18.04.1)
-- Dumped by pg_dump version 10.6 (Ubuntu 10.6-0ubuntu0.18.04.1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: fdistance(double precision, double precision, double precision, double precision); Type: FUNCTION; Schema: public; Owner: glonassd
--

CREATE FUNCTION public.fdistance(pnlat0 double precision, pnlon0 double precision, pnlat1 double precision, pnlon1 double precision) RETURNS double precision
    LANGUAGE plpgsql
    AS $$
DECLARE
-- расчет расстояния между двумя координатами
Distance double precision; -- расстояние в метрах

BEGIN
/*
Кратчайшее расстояние между двумя точками на земной поверхности (если принять ее за сферу) определяется зависимостью:
cos(d) = sin(fА) * sin(fB) + cos(fА) * cos(fB) * cos(lА - lB)
где
fА и fB — широты (в радианах),
lА, lB — долготы данных пунктов (в радианах),
d — расстояние между пунктами (в радианах)
перевод в метры осуществляется по формуле:
L = d * R
где R = 6371210.0 м — средний радиус земного шара в книгах русских авторов
и 6378137.0 м - в расчетах OpenStreet и OpenLayers для WGS84

ПыСы: расстояние между двумя точками на [земной поверхности] плоскости
вычисляется также по формуле
L = SQRT(POWER(x1 - x0, 2) + POWER(y1 - y0, 2)) (в метрах)
где (x0,y0) и (x1,y1) - координаты точек В МЕТРАХ, ибо плоскость
*/

with rad as (
select radians(pnLat0) as Lat0R, radians(pnLat1) as Lat1R
)
select
round((acos(sin(rad.Lat0R) * sin(rad.Lat1R) + cos(rad.Lat0R) * cos(rad.Lat1R) * cos(radians(pnLon0) - radians(pnLon1))) * 6378137)::numeric, 0)
into Distance
from rad; -- WGS84

RETURN Distance;
END;
$$;


ALTER FUNCTION public.fdistance(pnlat0 double precision, pnlon0 double precision, pnlat1 double precision, pnlon1 double precision) OWNER TO glonassd;

--
-- Name: fdistance2(double precision, double precision, double precision, double precision, integer); Type: FUNCTION; Schema: public; Owner: glonassd
--

CREATE FUNCTION public.fdistance2(pnlat0 double precision, pnlon0 double precision, pnlat1 double precision, pnlon1 double precision, pntime integer, OUT pndist integer, OUT pnbear integer, OUT pnsped integer) RETURNS record
    LANGUAGE plpgsql
    AS $$
DECLARE
/*
расчет расстояния, азимута и скорости между двумя координатами
пример вызова:
select gps.fdistance2(55.471336, 65.260344, 55.472128, 65.259508, 15);
select pnDist, pnBear, pnSped from gps.fdistance2(55.471336, 65.260344, 55.472128, 65.259508, 15);
*/
Distance double precision; -- расстояние в метрах
R integer;  -- средний радиус Земли, метров
Lat0R   double precision; -- в радианах
Lat1R   double precision; -- в радианах
Lon0R   double precision; -- в радианах
Lon1R   double precision; -- в радианах
deltalon double precision;    -- переменные для ускорения расчетов
deltalat double precision;

BEGIN
 R := 6378137; -- средний радиус земного шара в расчетах OpenStreet и OpenLayers для WGS84
 -- градусы в радианы
 Lat0R := pnLat0 * 0.0174532925;
 Lat1R := pnLat1 * 0.0174532925;
 Lon0R := pnLon0 * 0.0174532925;
 Lon1R := pnLon1 * 0.0174532925;
 -- для ускорения расчета
 deltalon := Lon1R - Lon0R;
 deltalat := Lat1R - Lat0R;

 -- 2. Формула гаверсинусов (Используется, чтобы избежать проблем с небольшими расстояниями, подвержена проблеме точек-антиподов)
 pnDist := round(((2 * ASIN(SQRT(POWER(SIN((deltalat)/2), 2) + COS(Lat0R) * COS(Lat1R) * POWER(SIN((deltalon)/2), 2)))) * R)::numeric, 0);

 -- расчет направления
 -- http://edu.dvgups.ru/METDOC/ITS/GEOD/LEK/l2/L3_1.htm
 -- Обратная геодезическая задача
 -- заключается в том, что при известных координатах точек А( XA, YA ) и В( XB, YB ) необходимо найти длину AB и направление линии АВ: румб и  дирекционный угол
 IF deltalat > 0 AND deltalon >= 0 THEN  -- 1 четверть (СВ) r = a
  pnBear := round((ATAN(deltalon/deltalat) * 57.2957795131)::numeric, 0); -- * 57.2957795131 - радианы в градусы
 ELSIF deltalat < 0 AND deltalon >= 0 THEN   -- 2 четверть (ЮВ) a = 180° – r
  pnBear := 180 - round((ABS(ATAN(deltalon/deltalat)) * 57.2957795131)::numeric, 0);
 ELSIF deltalat < 0 AND deltalon < 0 THEN    -- 3 четверть (ЮЗ) a = r + 180°
  pnBear := 180 + round((ABS(ATAN(deltalon/deltalat)) * 57.2957795131)::numeric, 0);
 ELSIF deltalat > 0 AND deltalon < 0 THEN    -- 4 четверть (СЗ) a = 360° – r
  pnBear := 360 - round((ABS(ATAN(deltalon/deltalat)) * 57.2957795131)::numeric, 0);
 ELSIF deltalat = 0 THEN
  if deltalon > 0 then -- 1 четверть (СВ)
   pnBear := 90;
  elsif deltalon < 0 then -- 3 четверть (ЮЗ)
   pnBear := 270;
  else
   pnBear := 0;
  end if;
 ELSE
  pnBear := 0;
 END IF;

 -- расчет скорости
 IF pnTime > 0 AND pnDist > 0 THEN
  pnSped := round((pnDist / pnTime * 3600 / 1000)::numeric, 0);
 ELSE
  pnSped := 0;
 END IF;
END;
$$;


ALTER FUNCTION public.fdistance2(pnlat0 double precision, pnlon0 double precision, pnlat1 double precision, pnlon1 double precision, pntime integer, OUT pndist integer, OUT pnbear integer, OUT pnsped integer) OWNER TO glonassd;

--
-- Name: ftime2str(integer, integer); Type: FUNCTION; Schema: public; Owner: glonassd
--

CREATE FUNCTION public.ftime2str(ntime integer DEFAULT 0, nseconds integer DEFAULT 0) RETURNS character varying
    LANGUAGE plpgsql
    AS $$
DECLARE

cTime varchar(8);

BEGIN
/*
FM "Fill mode". This modifier suppresses blank padding in the return value of the TO_CHAR function
FX "Format exact". This modifier specifies exact matching for the character argument and date format model of a TO_DATE function
http://download.oracle.com/docs/cd/B10501_01/server.920/a96540/sql_elements4a.htm#35387
*/
 BEGIN
  IF nSeconds > 0 THEN
   -- время в секундах от начала суток
   cTime := TO_CHAR(TRUNC(nTime / 3600), 'fm09') || ':' || TO_CHAR(TRUNC(MOD(nTime, 3600) / 60), 'fm09') || ':' || TO_CHAR(MOD(MOD(nTime, 3600), 60), 'fm09');
   ELSE
   -- время в минутах от начала суток
    cTime := TO_CHAR(TRUNC(nTime / 3600), 'fm09') || ':' || TO_CHAR(TRUNC(MOD(nTime, 3600) / 60), 'fm09');
   END IF;
  EXCEPTION
  WHEN OTHERS THEN
   cTime := 'Ошибка';
  END;
RETURN cTime;
END;
$$;


ALTER FUNCTION public.ftime2str(ntime integer, nseconds integer) OWNER TO glonassd;

--
-- Name: trigger_fct_tgpsdata_ainsert(); Type: FUNCTION; Schema: public; Owner: glonassd
--

CREATE OR REPLACE FUNCTION public.trigger_fct_tgpsdata_ainsert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
DECLARE
cur_data DATE; -- текущая дата
preMark RECORD; -- предыдущая отметка
bPremarkIsOlder BOOLEAN; -- предыдущая отметка старее текущей
dprobegc double precision; -- расстояние от предыдущей отметки в метрах

BEGIN
    select current_date into cur_data;

    IF NEW.ddata <= cur_data THEN -- дата отметки правильная

        -- проверяем дату и время предыдущей записи трекера
        select ddata, ntime, nlatitude, nlongitude
        into preMark  -- NULL, если запрос не вернул строк
        from ttrackerlog
        where cimei = NEW.cimei;

        IF preMark IS NULL THEN -- нет предыдущей отметки
            bPremarkIsOlder := FALSE;
        ELSIF preMark.ddata IS NULL THEN
            bPremarkIsOlder := TRUE;
        ELSE
            bPremarkIsOlder := NEW.ddata > preMark.ddata OR (NEW.ddata = preMark.ddata AND NEW.ntime > preMark.ntime);
        END IF;

        IF bPremarkIsOlder AND NEW.ddata = preMark.ddata THEN
            -- вычисляем пройденное от последней отметки расстояние
            BEGIN
                select fdistance(preMark.nlatitude, preMark.nlongitude, NEW.nlatitude, NEW.nlongitude)
                into dprobegc;

                IF (NEW.ntime - preMark.ntime) > 0 AND dprobegc / (NEW.ntime - preMark.ntime) > 41 THEN
                    -- 41 м/с = 147,6 км.ч, автобус не может двигаться с такой скоростью
                    -- скорее всего это ошибка навигации (прыжки на стоянке, неправильный порядок оотметок и т.п.)
                    bPremarkIsOlder := FALSE;   -- пропустим отметку
                END IF;
            EXCEPTION
            WHEN others then
                dprobegc := 0;
            END;
        ELSE
            dprobegc := 0;
        END IF;

        -- запись отметки в лог:
        -- если ещё нет предшествующих отметок или текущая старше предшествующей, то записываем
        IF preMark IS NULL OR bPremarkIsOlder THEN
            BEGIN
                update ttrackerlog set
                dsysdata = NEW.dsysdata,
                ddata = NEW.ddata,
                ntime = NEW.ntime,
                cimei = NEW.cimei,
                nstatus = NEW.nstatus,
                nlongitude = NEW.nlongitude,
                cew = NEW.cew,
                nlatitude = NEW.nlatitude,
                cns = NEW.cns,
                naltitude = NEW.naltitude,
                nspeed = NEW.nspeed,
                nheading = NEW.nheading,
                nsat = NEW.nsat,
                nvalid = NEW.nvalid,
                nnum = NEW.nnum,
                nvbort = NEW.nvbort,
                nvbat = NEW.nvbat,
                ntmp = NEW.ntmp,
                nhdop = NEW.nhdop,
                nout = NEW.nout,
                ninp = NEW.ninp,
                nin0 = NEW.nin0,
                nin1 = NEW.nin1,
                nin2 = NEW.nin2,
                nin3 = NEW.nin3,
                nin4 = NEW.nin4,
                nin5 = NEW.nin5,
                nin6 = NEW.nin6,
                nin7 = NEW.nin7,
                nfuel1 = NEW.nfuel1,
                nfuel2 = NEW.nfuel2,
                nprobeg = NEW.nprobeg,
                nzaj = NEW.nzaj,
                nalarm = NEW.nalarm,
                nprobegc = dprobegc,
                nport = NEW.nport
                where cimei = NEW.cimei;

                if NOT FOUND then -- первая отметка
                    insert into ttrackerlog
                    (dsysdata,ddata,ntime,cimei,nstatus,nlongitude,cew,
                    nlatitude,cns,naltitude,nspeed,nheading,nsat,nvalid,
                    nnum,nvbort,nvbat,ntmp,nhdop,nout,ninp,nin0,
                    nin1,nin2,nin3,nin4,nin5,nin6,nin7,
                    nfuel1,nfuel2,nprobeg,nzaj,nalarm,nprobegc,nport)
                    values
                    (NEW.dsysdata,NEW.ddata,NEW.ntime,NEW.cimei,NEW.nstatus,NEW.nlongitude,NEW.cew,
                    NEW.nlatitude,NEW.cns,NEW.naltitude,NEW.nspeed,NEW.nheading,NEW.nsat,NEW.nvalid,
                    NEW.nnum,NEW.nvbort,NEW.nvbat,NEW.ntmp,NEW.nhdop,NEW.nout,NEW.ninp,NEW.nin0,
                    NEW.nin1,NEW.nin2,NEW.nin3,NEW.nin4,NEW.nin5,NEW.nin6,NEW.nin7,
                    NEW.nfuel1,NEW.nfuel2,NEW.nprobeg,NEW.nzaj,NEW.nalarm,dprobegc,NEW.nport);
                end if;

            EXCEPTION
            WHEN others then
                null;
            END;

        END IF; -- IF preMark IS NULL OR bPremarkIsOlder
        -- запись отметки в лог, конец

    END IF; -- NEW.ddata <= cur_data

    RETURN NEW;
END
$$;


ALTER FUNCTION public.trigger_fct_tgpsdata_ainsert() OWNER TO glonassd;

SET default_with_oids = false;

--
-- Name: tgpsdata; Type: TABLE; Schema: public; Owner: glonassd
--

CREATE TABLE public.tgpsdata (
    dsysdata timestamp without time zone DEFAULT ('now'::text)::timestamp without time zone,
    ddata timestamp without time zone,
    ntime integer,
    cimei character varying(15),
    nstatus integer,
    nlongitude double precision,
    cew character varying(1) DEFAULT 'E'::character varying,
    nlatitude double precision,
    cns character varying(1) DEFAULT 'N'::character varying,
    naltitude real,
    nspeed real,
    nheading integer,
    nsat integer,
    nvalid integer,
    nnum integer,
    nvbort real,
    nvbat real,
    ntmp real,
    nhdop real,
    nout integer,
    ninp integer,
    nin0 real,
    nin1 real,
    nin2 real,
    nin3 real,
    nin4 real,
    nin5 real,
    nin6 real,
    nin7 real,
    nfuel1 real,
    nfuel2 real,
    nprobeg real,
    nzaj integer,
    nalarm integer,
    nprobegc real,
    nport integer DEFAULT 0
);


ALTER TABLE public.tgpsdata OWNER TO glonassd;

--
-- Name: TABLE tgpsdata; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON TABLE public.tgpsdata IS 'Данные GPS';


--
-- Name: COLUMN tgpsdata.dsysdata; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.dsysdata IS 'Системная (серверная) дата записи';


--
-- Name: COLUMN tgpsdata.ddata; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.ddata IS 'Дата GPS';


--
-- Name: COLUMN tgpsdata.ntime; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.ntime IS 'Время GPS в секундах от начала суток';


--
-- Name: COLUMN tgpsdata.cimei; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.cimei IS 'IMEI или ID устройства';


--
-- Name: COLUMN tgpsdata.nstatus; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nstatus IS 'Состояние устройства GPS';


--
-- Name: COLUMN tgpsdata.nlongitude; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nlongitude IS 'Долгота в долях градусов';


--
-- Name: COLUMN tgpsdata.cew; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.cew IS 'Флаг долготы E/W';


--
-- Name: COLUMN tgpsdata.nlatitude; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nlatitude IS 'Широта в долях градусов';


--
-- Name: COLUMN tgpsdata.cns; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.cns IS 'Флаг широты N/S';


--
-- Name: COLUMN tgpsdata.naltitude; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.naltitude IS 'Высота, метры';


--
-- Name: COLUMN tgpsdata.nspeed; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nspeed IS 'Скорость, км/ч (1миля = 1.852 km)';


--
-- Name: COLUMN tgpsdata.nheading; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nheading IS 'Направление движения (азимут)';


--
-- Name: COLUMN tgpsdata.nsat; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nsat IS 'Кол-во спутников';


--
-- Name: COLUMN tgpsdata.nvalid; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nvalid IS '0-подозрительная запись, 1-норма';


--
-- Name: COLUMN tgpsdata.nnum; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nnum IS 'Порядковый № пакета GPS';


--
-- Name: COLUMN tgpsdata.nvbort; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nvbort IS 'Напряжение бортовое';


--
-- Name: COLUMN tgpsdata.nvbat; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nvbat IS 'Напряжение батареи устройства';


--
-- Name: COLUMN tgpsdata.ntmp; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.ntmp IS 'Температура устройства';


--
-- Name: COLUMN tgpsdata.nhdop; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nhdop IS 'HDOP';


--
-- Name: COLUMN tgpsdata.nout; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nout IS 'Битовое поле состояния управляющих контактов (выходов)';


--
-- Name: COLUMN tgpsdata.ninp; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.ninp IS 'Битовое поле состояния датчиков (входов)';


--
-- Name: COLUMN tgpsdata.nin0; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nin0 IS 'Состояние датчика (входа) № 1';


--
-- Name: COLUMN tgpsdata.nfuel1; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nfuel1 IS 'Показания датчика уровня топлива № 1';


--
-- Name: COLUMN tgpsdata.nprobeg; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nprobeg IS 'Пробег, расчитанный терминалом';


--
-- Name: COLUMN tgpsdata.nzaj; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nzaj IS 'Состояние зажигания';


--
-- Name: COLUMN tgpsdata.nalarm; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nalarm IS 'Состояние кнопки тревоги';


--
-- Name: COLUMN tgpsdata.nprobegc; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON COLUMN public.tgpsdata.nprobegc IS 'Пробег, расчитанный сервером, метры';


--
-- Name: ttrackerlog; Type: TABLE; Schema: public; Owner: glonassd
--

CREATE TABLE public.ttrackerlog (
    dsysdata timestamp without time zone,
    ddata timestamp without time zone,
    ntime integer,
    cimei character varying(15) NOT NULL,
    nstatus integer,
    nlongitude double precision,
    cew character varying(1) DEFAULT 'E'::character varying,
    nlatitude double precision,
    cns character varying(1) DEFAULT 'N'::character varying,
    naltitude real,
    nspeed real,
    nheading integer,
    nsat integer,
    nvalid integer,
    nnum integer,
    nvbort real,
    nvbat real,
    ntmp real,
    nhdop real,
    nout integer,
    ninp integer,
    nin0 real,
    nin1 real,
    nin2 real,
    nin3 real,
    nin4 real,
    nin5 real,
    nin6 real,
    nin7 real,
    nfuel1 real,
    nfuel2 real,
    nprobeg real,
    nzaj integer,
    nalarm integer,
    nprobegc real,
    nkapkan integer DEFAULT 0,
    nx integer DEFAULT 0,
    ny integer DEFAULT 0,
    cip character varying(15),
    nport integer DEFAULT 0
);


ALTER TABLE public.ttrackerlog OWNER TO glonassd;

--
-- Name: TABLE ttrackerlog; Type: COMMENT; Schema: public; Owner: glonassd
--

COMMENT ON TABLE public.ttrackerlog IS 'Последний сигнал трекера, назначение полей см. TGPSDATA';


--
-- Name: ttrackerlog ttrackerlog_pk; Type: CONSTRAINT; Schema: public; Owner: glonassd
--

ALTER TABLE ONLY public.ttrackerlog
    ADD CONSTRAINT ttrackerlog_pk PRIMARY KEY (cimei);


--
-- Name: igpsdata; Type: INDEX; Schema: public; Owner: glonassd
--

CREATE INDEX igpsdata ON public.tgpsdata USING btree (ddata, ntime, cimei, nvalid);


--
-- Name: itrackerlog; Type: INDEX; Schema: public; Owner: glonassd
--

CREATE INDEX itrackerlog ON public.ttrackerlog USING btree (ddata, ntime, nvalid, cimei);


--
-- Name: tgpsdata tgpsdata_ainsert; Type: TRIGGER; Schema: public; Owner: glonassd
--

CREATE TRIGGER tgpsdata_ainsert AFTER INSERT ON public.tgpsdata FOR EACH ROW EXECUTE PROCEDURE public.trigger_fct_tgpsdata_ainsert();



CREATE OR REPLACE FUNCTION public.pchangeworkdata(pdnewworkdata date) RETURNS void
    LANGUAGE plpgsql
    AS $_$
BEGIN
    -- очистка рабочих таблиц
    BEGIN
        DELETE FROM public.tgpsdata WHERE ddata <> pdnewworkdata;
        DELETE FROM public.ttrackerlog WHERE ddata > pdnewworkdata;
    EXCEPTION
        WHEN OTHERS THEN NULL;
    END;
END;
$_$;

--
-- PostgreSQL database dump complete
--

