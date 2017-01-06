CREATE TABLE tgpsdata (
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
    nprobegc real
);

COMMENT ON TABLE tgpsdata IS 'Данные GPS';
COMMENT ON COLUMN tgpsdata.dsysdata IS 'Системная (серверная) дата записи';
COMMENT ON COLUMN tgpsdata.ddata IS 'Дата GPS';
COMMENT ON COLUMN tgpsdata.ntime IS 'Время GPS в секундах от начала суток';
COMMENT ON COLUMN tgpsdata.cimei IS 'IMEI или ID устройства';
COMMENT ON COLUMN tgpsdata.nstatus IS 'Состояние устройства GPS';
COMMENT ON COLUMN tgpsdata.nlongitude IS 'Долгота в долях градусов';
COMMENT ON COLUMN tgpsdata.cew IS 'Флаг долготы E/W';
COMMENT ON COLUMN tgpsdata.nlatitude IS 'Широта в долях градусов';
COMMENT ON COLUMN tgpsdata.cns IS 'Флаг широты N/S';
COMMENT ON COLUMN tgpsdata.naltitude IS 'Высота, метры';
COMMENT ON COLUMN tgpsdata.nspeed IS 'Скорость, км/ч (1миля = 1.852 km)';
COMMENT ON COLUMN tgpsdata.nheading IS 'Направление движения (азимут)';
COMMENT ON COLUMN tgpsdata.nsat IS 'Кол-во спутников';
COMMENT ON COLUMN tgpsdata.nvalid IS '0-подозрительная запись, 1-норма';
COMMENT ON COLUMN tgpsdata.nnum IS 'Порядковый № пакета GPS';
COMMENT ON COLUMN tgpsdata.nvbort IS 'Напряжение бортовое';
COMMENT ON COLUMN tgpsdata.nvbat IS 'Напряжение батареи устройства';
COMMENT ON COLUMN tgpsdata.ntmp IS 'Температура устройства';
COMMENT ON COLUMN tgpsdata.nhdop IS 'HDOP';
COMMENT ON COLUMN tgpsdata.nout IS 'Битовое поле состояния управляющих контактов (выходов)';
COMMENT ON COLUMN tgpsdata.ninp IS 'Битовое поле состояния датчиков (входов)';
COMMENT ON COLUMN tgpsdata.nin0 IS 'Состояние датчика (входа) № 1';
COMMENT ON COLUMN tgpsdata.nfuel1 IS 'Показания датчика уровня топлива № 1';
COMMENT ON COLUMN tgpsdata.nprobeg IS 'Пробег, расчитанный терминалом';
COMMENT ON COLUMN tgpsdata.nzaj IS 'Состояние зажигания';
COMMENT ON COLUMN tgpsdata.nalarm IS 'Состояние кнопки тревоги';
COMMENT ON COLUMN tgpsdata.nprobegc IS 'Пробег, расчитанный сервером, метры';

CREATE INDEX igpsdata ON tgpsdata USING btree (ddata, ntime, cimei, nvalid);
