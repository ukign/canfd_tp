#ifndef _NETWORKLAYER_H
#define _NETWORKLAYER_H

#include<stdint.h>

typedef enum{
	N_OK,//sender and receiver
	N_TIMEOUT_A,//sender and receiver
	N_TIMEOUT_Bs,//sender only
	N_TIMEOUT_Cr,//receiver only
	N_WRONG_SN,//receiver only
	N_INVALID_FS,//sneder only
	N_UNEXP_PDU,//receiver only
	N_WFT_OVRN,//
	N_BUFFER_OVFLW,//sender only
	N_ERROR,//sender and receiver
}tp_result;

/* message type*/
#define	DIAGNOSTIC		0
#define	REMOTE_DIAG		1

/* addressing type*/
#define NORMAL_ADDRESS	0
#define	EXTEND_ADDRESS	1

/* target address type*/
#define	CAN_PHY			1//11 bits can
#define	CAN_FUN			2//11 bits can
#define	CANFD_PHY		3//11 bits canfd
#define	CANFD_FUN		4//11 bits canfd
#define	CAN_PHY_EX		5//29 bits can
#define	CAN_FUN_EX		6//29 bits can
#define	CANFD_PHY_EX	7//29 bits canfd
#define	CANFD_FUN_EX	8//29 bits canfd

	
typedef struct{
	uint8_t tp_sa;
	uint8_t tp_ta;
	uint8_t tp_ta_type;
	uint8_t tp_ae;
}tp_address_info;

typedef struct{
	uint8_t message_type;
	tp_address_info address_info;
	uint32_t length;
	uint8_t* data;
}tp_message;


typedef struct{
	tp_address_info ai;
	uint8_t dlc;
	uint8_t data[64];
}n_pdu;

void tp_task(void);

/*
*entity:from 0...TP_ENTITY_NUM - 1
*message from upper layer to be transmited
*/
tp_result tp_request(uint8_t entity,tp_message msg);

void tp_can_msg_in(uint8_t message_type, uint8_t address_type, tp_address_info ai, uint8_t length, uint8_t* data);

void tp_datalink_configrm(n_pdu pdu);

#endif
