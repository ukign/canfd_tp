

#include <stdio.h>
#include "can_tp.h"
#include "tp_cfg.h"

#ifndef TRUE
#define TRUE		1
#endif

#ifndef FALSE
#define FALSE		0
#endif

/* flow contorl type*/
#define	FC_CTS				0//continue to send
#define	FC_WAIT				1//wait
#define	FC_OVFLW			2//overflow

typedef enum{
	TP_IDLE,
	TX_WAIT_FF_CONF,
	TX_WAIT_FC,
	TX_WAIT_CF_REQ,
	TX_WAIT_CF_CONF,
	RX_WAIT_FC_REQ,
	RX_WAIT_FC_CONF,
	RX_WAIT_CF,
}tp_step;

typedef enum{
	DL_CONF_IDLE,
	DL_TX_FF_CONF,
	DL_TX_FC_IND,
	DL_TX_CF_REQ,
	DL_TX_CF_CONF,
	DL_RX_FC_REQ,
	DL_RX_FC_CONF,
	DL_RX_CF_IND,
}tp_datalink_confirm;

typedef struct{
	tp_step curr_tp_step;//TP层步骤
	tp_datalink_confirm tp_dl_confirm;//链路层反馈的confirm信息
	int32_t tp_st_timer;//用于CF发送的stmin定时器
	uint8_t tp_stmin;//发送CF的时间间隔
	uint8_t tp_block_size;//发送CF的块大小
	uint8_t transfer_buffer[DATA_BUFFER_SIZE];//用于发送或接收的缓存
	uint8_t funtion_buffer[64];//接收功能寻址的数据缓存
	uint32_t next_cf_position;//记录下一个CF的数据在transfer_buffer的起始位置
	uint8_t cf_sn;//下一个CF的SN值，适用于接收和发送
	uint8_t block_transfered;//在流控FC后已完成的CF的个数，适用于接收和发送
	uint8_t rx_dl;//接收数据的CAN报文长度，已FF的DLC为准，后续CF判断需用到此值
	uint8_t fc_wait_times;//已接收到的FC，WAIT的次数
	tp_message curr_msg;//当前交互的信息，用于收发
	int32_t tp_timer;//实例的定时器，用于超时判断
	n_pdu curr_pdu_sending;//当前待发送的CAN消息
}tp_entity_block;

static tp_entity_block tp_entities[TP_ENTITY_NUM];

void tp_start_timer(uint8_t entity,int32_t counter)
{
	tp_entities[entity].tp_timer = counter;
}

void tp_start_st_timer(uint8_t entity, int32_t counter)
{
	tp_entities[entity].tp_st_timer = counter;
}

uint8_t tp_get_max_tx_len(uint8_t entity)
{
	if (tp_cfgs[entity].msg_type == DIAGNOSTIC)
	{
		if (tp_cfgs[entity].address_type == NORMAL_ADDRESS)//normal addressing
		{
			return tp_cfgs[entity].tx_dl;
		}
		else//extended addressing
		{
			return tp_cfgs[entity].tx_dl - 1;
		}
	}
	else if (tp_cfgs[entity].msg_type == REMOTE_DIAG)//mixed addressing
	{
		return tp_cfgs[entity].tx_dl - 1;
	}
	else
	{
		return 0;
	}
}

uint8_t tp_get_max_rx_len(uint8_t entity)
{
	if (tp_cfgs[entity].msg_type == DIAGNOSTIC)
	{
		if (tp_cfgs[entity].address_type == NORMAL_ADDRESS)//normal addressing
		{
			return tp_entities[entity].rx_dl;
		}
		else//extended addressing
		{
			return tp_entities[entity].rx_dl - 1;
		}
	}
	else if (tp_cfgs[entity].msg_type == REMOTE_DIAG)//mixed addressing
	{
		return tp_entities[entity].rx_dl - 1;
	}
	else
	{
		return 0;
	}
}

uint8_t tp_get_max_sf_tx_len(uint8_t entity)
{
	uint8_t max_data_len = tp_get_max_tx_len(entity);

	if (tp_cfgs[entity].tx_dl <= 8)
	{
		return max_data_len - 1;
	}
	else
	{
		return max_data_len - 2;
	}
}

uint8_t tp_get_max_sf_rx_len(uint8_t entity)
{
	uint8_t max_data_len = tp_get_max_tx_len(entity);

	if (tp_entities[entity].rx_dl <= 8)
	{
		return max_data_len - 1;
	}
	else
	{
		return max_data_len - 2;
	}
}

void tp_confirm(uint8_t entity, uint8_t message_type, tp_address_info ai, tp_result result)
{
	printf("confim channel %d type = %d,result = %d\r\n", entity, message_type, result);
}

void tp_indication(uint8_t entity, tp_result result)
{
	int i;
	printf("channel %d indication type = %d,result = %d,len = %d", entity, tp_entities[entity].curr_msg.message_type, result, tp_entities[entity].curr_msg.length);
	if (result == N_OK)
	{
		for (i = 0; i < tp_entities[entity].curr_msg.length; i++)
		{
			printf(" %02x", tp_entities[entity].curr_msg.data[i]);
		}
	}
	printf("\r\n");
}

void tp_ff_indication(uint8_t entity, tp_result result)
{
	printf("channel %d ff_indication type = %d,result = %d,len = %d\r\n", entity, tp_entities[entity].curr_msg.message_type, result, tp_entities[entity].curr_msg.length);
}

void tp_datalink_configrm(n_pdu pdu)
{
	uint8_t i;
	for (i = 0; i < TP_ENTITY_NUM; i++)
	{
		if ((pdu.ai.tp_ta_type == tp_cfgs[i].ai.tp_ta_type)
			&& (pdu.ai.tp_ta == tp_cfgs[i].ai.tp_ta)
			&& (pdu.ai.tp_sa == tp_cfgs[i].ai.tp_sa))
		{
			uint8_t pci = (pdu.data[0] & 0xF0);
			if (pci == 0x00)
			{
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_OK);
			}
			else if (pci == 0x10)
			{
				tp_start_timer(i, tp_cfgs[i].timing.timeout_bs);
				tp_entities[i].curr_tp_step = TX_WAIT_FC;
				tp_entities[i].fc_wait_times = 0;
			}
			else if (pci == 0x20)
			{
				if (tp_entities[i].curr_msg.length > tp_entities[i].next_cf_position)
				{
					if ((tp_entities[i].block_transfered < tp_entities[i].tp_block_size) || (tp_entities[i].tp_block_size == 0))
					{
						tp_start_timer(i, tp_cfgs[i].timing.timeout_cs);
						tp_start_st_timer(i, 0);
						tp_entities[i].curr_tp_step = TX_WAIT_CF_REQ;
					}
					else
					{
						tp_start_timer(i, tp_cfgs[i].timing.timeout_bs);
						tp_entities[i].curr_tp_step = TX_WAIT_FC;
						tp_entities[i].fc_wait_times = 0;
					}
				}
				else
				{
					tp_entities[i].curr_tp_step = TP_IDLE;
					tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_OK);
				}
			}
			else if (pci == 0x30)
			{
				tp_entities[i].curr_tp_step = RX_WAIT_CF;
				tp_start_timer(i, tp_cfgs[i].timing.timeout_cr);
			}
		}
	}
}

#if LOOP_BACK_TEST
void tp_loop_back_test(n_pdu pdu)
{
	tp_can_msg_in(DIAGNOSTIC, NORMAL_ADDRESS, pdu.ai, pdu.dlc, pdu.data);
}
#endif

void tp_send_can_frame(uint8_t entity)
{
	//uint32_t id = 0x700 + tp_entities[entity].curr_pdu_sending.ai.tp_ta;
	//tp_entities[entity].curr_pdu_sending.dlc = tp_cfgs[entity].tx_dl;
	printf("channel %d %x->%x %d", entity, tp_entities[entity].curr_pdu_sending.ai.tp_sa, tp_entities[entity].curr_pdu_sending.ai.tp_ta, tp_cfgs[entity].tx_dl);
	uint8_t i;
	for (i = 0; i < tp_entities[entity].curr_pdu_sending.dlc; i++)
	{
		printf(" %02x", tp_entities[entity].curr_pdu_sending.data[i]);
	}
	printf("\r\n");

	if ((tp_entities[entity].curr_pdu_sending.data[0] & 0xF0) == 0x00)
	{
		tp_confirm(entity, tp_entities[entity].curr_msg.message_type, tp_entities[entity].curr_msg.address_info, N_OK);
	}
	else if ((tp_entities[entity].curr_pdu_sending.data[0] & 0xF0) == 0x10)
	{
		tp_entities[entity].tp_dl_confirm = DL_TX_FF_CONF;
	}
	else if ((tp_entities[entity].curr_pdu_sending.data[0] & 0xF0) == 0x20)
	{
		tp_entities[entity].tp_dl_confirm = DL_TX_CF_CONF;
	}
	else if ((tp_entities[entity].curr_pdu_sending.data[0] & 0xF0) == 0x30)
	{
		tp_entities[entity].tp_dl_confirm = DL_RX_FC_CONF;
	}

#if LOOP_BACK_TEST
	tp_loop_back_test(tp_entities[entity].curr_pdu_sending);
#endif
}

void tp_send_sf(uint8_t entity, tp_message msg)
{
	tp_entities[entity].curr_pdu_sending.dlc = tp_cfgs[entity].tx_dl;
	if (tp_cfgs[entity].msg_type == DIAGNOSTIC)
	{
		if (tp_cfgs[entity].address_type == NORMAL_ADDRESS)
		{
			if (tp_cfgs[entity].tx_dl <= 8)
			{
				tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
				tp_entities[entity].curr_pdu_sending.data[0] = msg.length;
				memcpy(&tp_entities[entity].curr_pdu_sending.data[1], msg.data, msg.length);
			}
			else
			{
				tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
				tp_entities[entity].curr_pdu_sending.data[0] = 0;
				tp_entities[entity].curr_pdu_sending.data[1] = msg.length;
				memcpy(&tp_entities[entity].curr_pdu_sending.data[2], msg.data, msg.length);
			}
			tp_send_can_frame(entity);
			tp_start_timer(entity,tp_cfgs[entity].timing.timeout_as);
		}
		else
		{
			tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
			tp_entities[entity].curr_pdu_sending.data[0] = msg.address_info.tp_ta;
			tp_entities[entity].curr_pdu_sending.data[1] = msg.length;
			memcpy(&tp_entities[entity].curr_pdu_sending.data[2], msg.data, msg.length);
		}
	}
	else if (msg.message_type == REMOTE_DIAG)
	{
		if (tp_cfgs[entity].tx_dl <= 8)
		{
			tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
			tp_entities[entity].curr_pdu_sending.data[0] = msg.address_info.tp_ae;
			tp_entities[entity].curr_pdu_sending.data[1] = msg.length;
			memcpy(&tp_entities[entity].curr_pdu_sending.data[2], msg.data, msg.length);
		}
	}
}

void tp_send_ff(uint8_t entity, tp_message msg)
{
	tp_entities[entity].curr_pdu_sending.dlc = tp_cfgs[entity].tx_dl;
	if (tp_cfgs[entity].msg_type == DIAGNOSTIC)
	{
		if (tp_cfgs[entity].address_type == NORMAL_ADDRESS)
		{
			if (msg.length <= 4095)
			{
				tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
				tp_entities[entity].curr_pdu_sending.data[0] = 0x10 + (msg.length >> 8);
				tp_entities[entity].curr_pdu_sending.data[1] = (msg.length & 0xFF);
				memcpy(&tp_entities[entity].curr_pdu_sending.data[2], msg.data, tp_cfgs[entity].tx_dl - 2);
				memcpy(&tp_entities[entity].transfer_buffer, msg.data, msg.length);
				tp_entities[entity].next_cf_position = tp_cfgs[entity].tx_dl - 2;
				tp_entities[entity].curr_msg.length = msg.length;
				tp_entities[entity].cf_sn = 1;
			}
			else
			{
				tp_entities[entity].curr_pdu_sending.ai = msg.address_info;
				tp_entities[entity].curr_pdu_sending.data[0] = 0x10;
				tp_entities[entity].curr_pdu_sending.data[1] = 0x00;
				tp_entities[entity].curr_pdu_sending.data[2] = (msg.length >> 24) & 0xFF;
				tp_entities[entity].curr_pdu_sending.data[3] = (msg.length >> 16) & 0xFF;
				tp_entities[entity].curr_pdu_sending.data[4] = (msg.length >> 8) & 0xFF;
				tp_entities[entity].curr_pdu_sending.data[5] = msg.length & 0xFF;
				memcpy(&tp_entities[entity].curr_pdu_sending.data[6], msg.data, tp_cfgs[entity].tx_dl - 6);
				memcpy(&tp_entities[entity].transfer_buffer, msg.data, msg.length);
				tp_entities[entity].next_cf_position = tp_cfgs[entity].tx_dl - 6;
				tp_entities[entity].curr_msg.length = msg.length;
				tp_entities[entity].cf_sn = 1;
			}
			tp_start_timer(entity,tp_cfgs[entity].timing.timeout_as);
			tp_entities[entity].curr_tp_step = TX_WAIT_FF_CONF;
			tp_send_can_frame(entity);
		}
		else
		{
			
		}
	}
	else if (msg.message_type == REMOTE_DIAG)
	{
		if (tp_cfgs[entity].tx_dl <= 8)
		{
			
		}
	}
}

void tp_send_CF(uint8_t entity)
{
	uint8_t max_data_len = tp_get_max_tx_len(entity);
	tp_entities[entity].curr_pdu_sending.data[0] = 0x20 + (tp_entities[entity].cf_sn & 0x0F);
	uint8_t curr_valid_data_len;
	if ((tp_entities[entity].curr_msg.length - tp_entities[entity].next_cf_position) >= (max_data_len - 1))
	{
		curr_valid_data_len = max_data_len - 1;
		tp_entities[entity].curr_pdu_sending.dlc = tp_cfgs[entity].tx_dl;
	}
	else
	{
		curr_valid_data_len = tp_entities[entity].curr_msg.length - tp_entities[entity].next_cf_position;
		memset(&tp_entities[entity].curr_pdu_sending.data[curr_valid_data_len + 1], tp_cfgs[entity].padding_bytes, max_data_len - curr_valid_data_len - 1);
		tp_entities[entity].curr_pdu_sending.dlc = curr_valid_data_len + 1;
	}

	memcpy(&tp_entities[entity].curr_pdu_sending.data[1], &tp_entities[entity].transfer_buffer[tp_entities[entity].next_cf_position], curr_valid_data_len);
	tp_entities[entity].next_cf_position += curr_valid_data_len;
	tp_entities[entity].block_transfered++;
	tp_entities[entity].cf_sn++;

	tp_send_can_frame(entity);
}

void tp_send_fc(uint8_t entity ,uint8_t type)
{
	tp_entities[entity].curr_pdu_sending.ai = tp_cfgs[entity].ai;
	tp_entities[entity].curr_pdu_sending.data[0] = 0x30 + (type & 0x0F);
	tp_entities[entity].curr_pdu_sending.data[1] = tp_cfgs[entity].bs_on_rx;
	tp_entities[entity].curr_pdu_sending.data[2] = tp_cfgs[entity].stmin_on_rx;

	if (tp_cfgs[entity].enable_padding)
	{
		tp_entities[entity].curr_pdu_sending.dlc = tp_cfgs[entity].tx_dl;
		memset(&tp_entities[entity].curr_pdu_sending.data[3], tp_cfgs[entity].padding_bytes, tp_cfgs[entity].tx_dl - 3);
	}
	else
	{
		tp_entities[entity].curr_pdu_sending.dlc = 3;
	}
	tp_send_can_frame(entity);
}

tp_result tp_request(uint8_t entity,tp_message msg)
{
	tp_result result = N_OK;
	//TODO:增加msg地址信息和配置地址信息的判断
	if ((entity >= TP_ENTITY_NUM) 
		|| (msg.length > DATA_BUFFER_SIZE)
		|| (msg.message_type != tp_cfgs[entity].msg_type)
		|| (msg.address_info.tp_ta_type != tp_cfgs[entity].ai.tp_ta_type)
		|| (msg.address_info.tp_ta != tp_cfgs[entity].ai.tp_ta)
		|| (msg.address_info.tp_sa != tp_cfgs[entity].ai.tp_sa))
	{
		result = N_BUFFER_OVFLW;
	}
	else
	{
		uint8_t max_sf_len = tp_get_max_sf_tx_len(entity);
		if (msg.length <= max_sf_len)
		{
			tp_send_sf(entity, msg);
		}
		else
		{
			tp_send_ff(entity, msg);
		}	
		tp_entities[entity].curr_msg = msg;
	}
	return result;
}

void tp_parse_sf(uint8_t entity, tp_address_info ai, uint8_t length, uint8_t* data)
{
	tp_result result = N_UNEXP_PDU;
	switch (tp_entities[entity].curr_tp_step)
	{
	case TP_IDLE:
		result = N_OK;
	case RX_WAIT_FC_REQ:
	case RX_WAIT_FC_CONF:
	case RX_WAIT_CF:
		if (length == 8)
		{
			uint8_t len = (data[0] & 0x0F);
			if (len >= 1 && len <= 7)
			{
				memcpy(tp_entities[entity].transfer_buffer, data + 1, len);
				tp_entities[entity].curr_msg.data = tp_entities[entity].transfer_buffer;
				tp_entities[entity].curr_msg.length = len;
				tp_indication(entity, result);
			}
		}
		else if ((length == 12)
			|| (length == 16)
			|| (length == 20)
			|| (length == 24)
			|| (length == 32)
			|| (length == 48)
			|| (length == 64))
		{
			uint8_t len = (data[0] & 0x0F);
			if (len == 0)
			{
				len = data[1];
				if (((ai.tp_ta_type == CANFD_PHY)
					|| (ai.tp_ta_type == CANFD_FUN)
					|| (ai.tp_ta_type == CANFD_PHY_EX)
					|| (ai.tp_ta_type == CANFD_FUN_EX))
					&& (len <= length - 2))
				{
					memcpy(tp_entities[entity].transfer_buffer, data + 2, len); 
					tp_entities[entity].curr_msg.data = tp_entities[entity].transfer_buffer;
					tp_entities[entity].curr_msg.length = len;
					tp_indication(entity, result);
				}
			}
		}
		tp_entities[entity].curr_tp_step = TP_IDLE;
		break;
	case TX_WAIT_FF_CONF:
	case TX_WAIT_FC:
	case TX_WAIT_CF_REQ:
	case TX_WAIT_CF_CONF:
		if (length == 8)
		{
			if ((ai.tp_ta_type == CAN_FUN) || (ai.tp_ta_type == CAN_FUN_EX))
			{
				uint8_t len = (data[0] & 0x0F);
				if (len >= 1 && len <= 7)
				{
					memcpy(tp_entities[entity].funtion_buffer, data + 1, len);
					tp_entities[entity].curr_msg.data = tp_entities[entity].funtion_buffer;
					tp_entities[entity].curr_msg.length = len;
					tp_indication(entity, result);
				}
			}
		}
		else if ((length == 12)
			|| (length == 16)
			|| (length == 20)
			|| (length == 24)
			|| (length == 32)
			|| (length == 48)
			|| (length == 64))
		{
			uint8_t len = (data[0] & 0x0F);
			if (len == 0)
			{
				len = data[1];
				if (((ai.tp_ta_type == CANFD_FUN) || (ai.tp_ta_type == CANFD_FUN_EX)) && (len <= length - 2))
				{
					memcpy(tp_entities[entity].funtion_buffer, data + 2, len);
					tp_entities[entity].curr_msg.data = tp_entities[entity].funtion_buffer;
					tp_entities[entity].curr_msg.length = len;
					tp_indication(entity, result);
				}
			}
		}
		break;
	default:
		break;
	}
}

void tp_parse_ff(uint8_t entity, tp_address_info ai, uint8_t length, uint8_t* data)
{
	tp_result result = N_UNEXP_PDU;
	switch (tp_entities[entity].curr_tp_step)
	{
	case TP_IDLE:
		result = N_OK;
	case RX_WAIT_FC_REQ:
	case RX_WAIT_FC_CONF:
	case RX_WAIT_CF:
		if ((length == 8)
			||(length == 12)
			|| (length == 16)
			|| (length == 20)
			|| (length == 24)
			|| (length == 32)
			|| (length == 48)
			|| (length == 64))
		{
			tp_entities[entity].rx_dl = length;

			uint32_t data_len = ((data[0] & 0x0F) << 8) + data[1];
			if (data_len == 0)
			{
				data_len = (data[2] << 24) + (data[3] << 16) + (data[4] << 8) + data[5];
				tp_entities[entity].curr_msg.length = data_len;
				tp_entities[entity].curr_msg.data = tp_entities[entity].transfer_buffer;
				memcpy(tp_entities[entity].transfer_buffer, data + 6, length- 6);
				tp_entities[entity].next_cf_position = length - 6;
				tp_entities[entity].curr_tp_step = RX_WAIT_FC_REQ;
				tp_entities[entity].block_transfered = 0;
				tp_entities[entity].cf_sn = 1;
				tp_ff_indication(entity, result);
			}
			else if (data_len > tp_get_max_sf_rx_len(entity))
			{
				tp_entities[entity].curr_msg.length = data_len;
				tp_entities[entity].curr_msg.data = tp_entities[entity].transfer_buffer;
				memcpy(tp_entities[entity].transfer_buffer, data + 2, length - 2);
				tp_entities[entity].next_cf_position = length - 2;
				tp_entities[entity].curr_tp_step = RX_WAIT_FC_REQ;
				tp_entities[entity].block_transfered = 0;
				tp_entities[entity].cf_sn = 1;
				tp_ff_indication(entity, result);
			}
		}
		break;
	default:
		break;
	}
}

void tp_parse_cf(uint8_t entity, tp_address_info ai, uint8_t length, uint8_t* data)
{
	switch (tp_entities[entity].curr_tp_step)
	{
	case RX_WAIT_CF:
		if ((length == tp_entities[entity].rx_dl)//CF的CAN_DL与已接收的FF的CAN_DL相等
			|| ((length -1 + tp_entities[entity].next_cf_position) == tp_entities[entity].curr_msg.length))//此CF时分段信息的最后一条
		{
			uint8_t sn = (data[0] & 0x0F);
			if ((tp_entities[entity].cf_sn & 0x0F) == sn)
			{
				memcpy(tp_entities[entity].transfer_buffer + tp_entities[entity].next_cf_position, data + 1, length - 1);
				tp_entities[entity].next_cf_position += (length - 1);
				if (tp_entities[entity].next_cf_position >= tp_entities[entity].curr_msg.length)//分段信息传输完成
				{
					tp_entities[entity].curr_tp_step = TP_IDLE;
					tp_indication(entity, N_OK);
				}
				else
				{
					tp_entities[entity].cf_sn++;
					tp_entities[entity].block_transfered++;
					tp_entities[entity].tp_dl_confirm = DL_RX_CF_IND;
				}
			}
			else
			{
				tp_indication(entity, N_WRONG_SN);
			}
		}
		break;
	default:
		break;
	}
}

void tp_parse_fc(uint8_t entity, tp_address_info ai, uint8_t length, uint8_t* data)
{
	switch (tp_entities[entity].curr_tp_step)
	{
	case TX_WAIT_FC:
		if ((data[0] & 0x0F) == FC_CTS)
		{
			tp_entities[entity].tp_block_size = data[1];

			if ((data[2] >= 0x80 && data[2] <= 0xF0) || (data[2] >= 0xFA))
			{
				tp_entities[entity].tp_stmin = 0x7F;
			}
			else if (data[2] >= 0xF1 && data[2] <= 0xF9)
			{
				tp_entities[entity].tp_stmin = 0x1;
			}
			else
			{
				tp_entities[entity].tp_stmin = data[2];
			}
			tp_entities[entity].tp_dl_confirm = DL_TX_FC_IND;
			tp_entities[entity].block_transfered = 0;
		}
		else if ((data[0] & 0x0F) == FC_WAIT)
		{
			if (tp_entities[entity].fc_wait_times < tp_cfgs[entity].max_fc_wait_times)
			{
				tp_entities[entity].fc_wait_times++;
				tp_start_timer(entity, tp_cfgs[entity].timing.timeout_bs);
			}
			else
			{
				tp_indication(entity, N_WFT_OVRN);
			}
		}
		else if ((data[0] & 0x0F) == FC_OVFLW)
		{
			tp_confirm(entity, tp_entities[entity].curr_msg.message_type, tp_entities[entity].curr_msg.address_info, N_BUFFER_OVFLW);
		}
		else
		{
			tp_confirm(entity, tp_entities[entity].curr_msg.message_type, tp_entities[entity].curr_msg.address_info, N_INVALID_FS);
		}
		break;
	default:
		break;
	}
}

void tp_can_msg_in(uint8_t message_type, uint8_t address_type, tp_address_info ai, uint8_t length, uint8_t* data)
{
	uint8_t i;
	for (i = 0; i < TP_ENTITY_NUM; i++)
	{
		if (message_type == tp_cfgs[i].msg_type && address_type == tp_cfgs[i].address_type)
		{
			//if ((ai.tp_ta_type == tp_cfgs[i].ta_type) && (ai.tp_sa == tp_cfgs[i].remote_address))
			if ((ai.tp_sa == tp_cfgs[i].ai.tp_ta))
			{
				uint8_t pci = data[0] >> 4;
				if (ai.tp_ta == tp_cfgs[i].ai.tp_sa)
				{
					if ((ai.tp_ta_type == CAN_PHY)
						|| (ai.tp_ta_type == CANFD_PHY)
						|| (ai.tp_ta_type == CAN_PHY_EX)
						|| (ai.tp_ta_type == CANFD_PHY_EX))
					{
						if (pci == 0)
						{
							tp_entities[i].curr_msg.message_type = message_type;
							tp_entities[i].curr_msg.address_info = ai;
							tp_parse_sf(i, ai, length, data);
						}
						else if (pci == 1)
						{
							tp_entities[i].curr_msg.message_type = message_type;
							tp_entities[i].curr_msg.address_info = ai;
							tp_parse_ff(i, ai, length, data);
						}
						else if (pci == 2)
						{
							tp_entities[i].curr_msg.message_type = message_type;
							tp_entities[i].curr_msg.address_info = ai;
							tp_parse_cf(i, ai, length, data);
						}
						else if (pci == 3)
						{
							tp_entities[i].curr_msg.message_type = message_type;
							tp_entities[i].curr_msg.address_info = ai;
							tp_parse_fc(i, ai, length, data);
						}
					}
				}				
				else if (ai.tp_ta == tp_cfgs[i].broadcast_address)
				{
					if ((ai.tp_ta_type == CAN_FUN)
						|| (ai.tp_ta_type == CANFD_FUN)
						|| (ai.tp_ta_type == CAN_FUN_EX)
						|| (ai.tp_ta_type == CANFD_FUN_EX))
					{
						if (pci == 0)
						{
							tp_entities[i].curr_msg.message_type = message_type;
							tp_entities[i].curr_msg.address_info = ai;
							tp_parse_sf(i, ai, length, data);
						}
					}
				}
			}
		}
	}
}

void tp_task(void)
{
	int i;	
	for (i = 0; i < TP_ENTITY_NUM; i++)
	{
		tp_entities[i].tp_timer -= TP_TASK_PEROID;
		switch (tp_entities[i].curr_tp_step)
		{
		case TX_WAIT_FF_CONF:
			if (tp_entities[i].tp_dl_confirm == DL_TX_FF_CONF)
			{
				tp_start_timer(i, tp_cfgs[i].timing.timeout_bs);
				tp_entities[i].curr_tp_step = TX_WAIT_FC;
				tp_entities[i].fc_wait_times = 0;
			}
			else if (tp_entities[i].tp_timer <= 0)
			{
				tp_entities[i].curr_tp_step = TP_IDLE;
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_TIMEOUT_A);
			}
			break;
		case TX_WAIT_FC:
			if (tp_entities[i].tp_dl_confirm == DL_TX_FC_IND)
			{
				tp_start_timer(i, tp_cfgs[i].timing.timeout_cs);
				tp_start_st_timer(i,0);
				tp_entities[i].curr_tp_step = TX_WAIT_CF_REQ;
			}
			else if (tp_entities[i].tp_timer <= 0)
			{
				tp_entities[i].curr_tp_step = TP_IDLE;
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_TIMEOUT_Bs);
			}
			break;
		case TX_WAIT_CF_REQ:
			tp_entities[i].tp_st_timer -= TP_TASK_PEROID;
			if (tp_entities[i].tp_st_timer <= 0)
			{
				tp_send_CF(i);
				tp_start_timer(i, tp_cfgs[i].timing.timeout_as);
				tp_start_st_timer(i, tp_entities[i].tp_stmin);
				tp_entities[i].curr_tp_step = TX_WAIT_CF_CONF;
			}
			break;
		case TX_WAIT_CF_CONF:
			tp_entities[i].tp_st_timer -= TP_TASK_PEROID;
			if (tp_entities[i].tp_dl_confirm == DL_TX_CF_CONF)
			{
				if (tp_entities[i].curr_msg.length > tp_entities[i].next_cf_position)
				{
					if ((tp_entities[i].block_transfered < tp_entities[i].tp_block_size) || (tp_entities[i].tp_block_size == 0))
					{
						tp_start_timer(i, tp_cfgs[i].timing.timeout_cs);
						tp_start_st_timer(i, 0);
						tp_entities[i].curr_tp_step = TX_WAIT_CF_REQ;
					}
					else
					{
						tp_start_timer(i, tp_cfgs[i].timing.timeout_bs);
						tp_entities[i].curr_tp_step = TX_WAIT_FC;
						tp_entities[i].fc_wait_times = 0;
					}
				}
				else
				{
					tp_entities[i].curr_tp_step = TP_IDLE;
					tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_OK);
				}
			}
			else if (tp_entities[i].tp_timer <= 0)
			{
				tp_entities[i].curr_tp_step = TP_IDLE;
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_TIMEOUT_A);
			}
			break;
		case RX_WAIT_FC_REQ:
			if (tp_entities[i].curr_msg.length > DATA_BUFFER_SIZE)
			{
				tp_send_fc(i, FC_OVFLW);
				tp_entities[i].curr_tp_step = TP_IDLE;
			}
			else
			{
				tp_send_fc(i, FC_CTS);
				tp_start_timer(i, tp_cfgs[i].timing.timeout_ar);
				tp_entities[i].curr_tp_step = RX_WAIT_FC_CONF;
			}
			break;
		case RX_WAIT_FC_CONF:
			if (tp_entities[i].tp_dl_confirm == DL_RX_FC_CONF)
			{
				tp_entities[i].curr_tp_step = RX_WAIT_CF;
				tp_start_timer(i, tp_cfgs[i].timing.timeout_cr);
			}
			else if (tp_entities[i].tp_timer <= 0)
			{
				tp_entities[i].curr_tp_step = TP_IDLE;
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_TIMEOUT_A);
			}
			break;
		case RX_WAIT_CF:
			if (tp_entities[i].tp_dl_confirm == DL_RX_CF_IND)
			{
				if ((tp_cfgs[i].bs_on_rx != 0) && (tp_entities[i].block_transfered >= tp_cfgs[i].bs_on_rx))
				{
					tp_entities[i].curr_tp_step = RX_WAIT_FC_REQ;
					tp_entities[i].block_transfered = 0;
					tp_start_timer(i, tp_cfgs[i].timing.timeout_br);
				}
				else
				{
					tp_entities[i].tp_dl_confirm = DL_CONF_IDLE;
					tp_start_timer(i, tp_cfgs[i].timing.timeout_cr);
				}
			}
			else if (tp_entities[i].tp_timer <= 0)
			{
				tp_entities[i].curr_tp_step = TP_IDLE;
				tp_confirm(i, tp_entities[i].curr_msg.message_type, tp_entities[i].curr_msg.address_info, N_TIMEOUT_Cr);
			}
			break;
		default:
			break;
		}
	}
}