-- перевод рабочей даты (выполнять после полуночи)
begin DISPATCHER.PCHANGEWORKDATA_TEST(TRUNC(CURRENT_DATE), 12); end;