# canfd_tp
can tranport layer demo based on iso 15765-2 2016,canfd support in this version

1.this is visual studio 2019 project,you can run it in windows and then varify the TP logic.
2.when LOOP_BACK_TEST(tp_cfg.h file) must 1 if you want to check the logic,and you remove the macro if you want to run on real envoriment
3.there must be 2 entities(tp_cfgs[] in tp_cfg.h file) when runs on windows,one for receive node and another for tranmit node.
4.if you want to run it on real envoriment ,tp_confirm,tp_indication and tp_ff_indication(can_tp.c) must be modified to fit UDS session layer
5.if you want to run it on real envoriment ,tp_send_can_frame(can_tp.c)must to change to interface with CAN send driver.
