#ifndef _TP_CFG_H
#define _TP_CFG_H

#include "can_tp.h"

typedef struct{
	int32_t timeout_as;
	int32_t timeout_ar;
	int32_t timeout_bs;
	int32_t timeout_br;
	int32_t timeout_cs;
	int32_t timeout_cr;
}tp_time_config;

typedef struct{
	uint8_t msg_type;//消息类型：诊断或远程诊断
	uint8_t address_type;//寻址类型：消息类型=诊断 normal或extend，消息类型=远程诊断是，只能值为mixed
	tp_address_info ai;//地址信息：地址类型，源地址，目的地址
	uint8_t broadcast_address;//广播地址，及功能寻址请求的TA
	uint8_t tx_dl;//数据链路层长度，及CAN报文的长度，可以是8 12 16 20 24 32 48 64
	uint8_t stmin_on_rx;//接收数据时，CF帧间的最小间隔时间，以毫秒为单位
	uint8_t bs_on_rx;//接收数据时的BS大小
	uint8_t enable_padding;//是否允许进行数据填充，0:禁止，1：使能数据填充
	uint8_t padding_bytes;//使能数据填充时，填充的字节
	uint8_t max_fc_wait_times;//接收数据时，允许类型为WAIT的流控制帧的个数
	tp_time_config timing;
}tp_config_type;

#define TP_TASK_PEROID			1//peroid in mill second

const tp_config_type tp_cfgs[] = {
	{
		DIAGNOSTIC,
		NORMAL_ADDRESS,
		{
			0x12,
			0x10,
			CANFD_PHY,
			0x00,
		},
		0xDF,
		32,
		1,
		8,
		1,
		0xAA,
		5,
		{
			1000,
			1000,
			1000,
			1000,
			1000,
			1000,
		},
	},
	{
		DIAGNOSTIC,
		NORMAL_ADDRESS,
		{
			0x10,
			0x12,
			CANFD_PHY,
			0x00,
		},
		0xDF,
		24,
		1,
		4,
		1,
		0xCC,
		5,
		{
			1000,
			1000,
			1000,
			1000,
			1000,
			1000,
		},
	},
};

#define TP_ENTITY_NUM			(sizeof(tp_cfgs)/sizeof(tp_config_type))//TP层的示例数目

#define LOOP_BACK_TEST			1//是否使能测试，1：使能还回测试，此时至少 位置两个实例，一个发送一个接收

#define DATA_BUFFER_SIZE		5000//最大缓存大小，表示UDS请求或响应的最大长度，每个实例都一样

/*==============================================================================*/

#endif