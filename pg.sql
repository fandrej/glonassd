INSERT INTO gps.tgpsdata (
	ddata,          --$1
	ntime,
	cimei,          --$3
	nstatus,
	nlongitude,     --$5
	cew,
	nlatitude,      --$7
	cns,
	naltitude,      --$9
	nspeed,
	nheading,       --$11
	nsat,
	nvalid,         --$13
	nnum,
	nvbort,         --$15
	nvbat,
	ntmp,           --$17
	nhdop,
	nout,           --$19
	ninp,
	nin0,           --$21
	nin1,
	nin2,           --$23
	nin3,
	nin4,           --$25
	nin5,
	nin6,           --$27
	nin7,
	nfuel1,         --$29
	nfuel2,
	nprobeg,        --$31
	nzaj,
	nalarm,         --$33
    cmessage
) VALUES (
	to_timestamp($1::bigint),
	$2::integer,
	$3::varchar,
	$4::integer,
	$5::double precision,
	$6::varchar,
	$7::double precision,
	$8::varchar,
	$9::real,
	$10::real,
	$11::integer,
	$12::integer,
	$13::integer,
	$14::integer,
	$15::real,
	$16::real,
	$17::real,
	$18::real,
	$19: bigint,
	$20::bigint,
	$21::real,
	$22::real,
	$23::real,
	$24::real,
	$25::real,
	$26::real,
	$27::real,
	$28::real,
	$29::real,
	$30::real,
	$31::real,
	$32::integer,
	$33::integer,
	$34::varchar
);