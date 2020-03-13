#include <stdio.h>
#include <Windows.h>

#include "can_tp.h"

uint8_t data[5000];
uint8_t data2[256];
uint8_t can_msg[16] = {0x10,0x0E,0x10,0x03};

tp_message msg = {
	DIAGNOSTIC,
	{
		0x12,
		0x10,
		CANFD_PHY,
		0x00,
	},
	4096,
	data
};

tp_message msg1 = {
	DIAGNOSTIC,
	{
		0x10,
		0x12,
		CANFD_PHY,
		0x00,
	},
	255,
	data2
};

void init_data(void)
{
	int i;
	for (i = 0; i < 5000; i++)
	{
		data[i] = (uint8_t)(i & 0xFF);
	}

	for (i = 0; i < 256; i++)
	{
		data2[i] = (uint8_t)(0xFF - i);
	}
}

int main(void)
{
	init_data();

	//tp_address_info ai = { 0x10, 0x12, CANFD_PHY, 0 };
	//tp_can_msg_in(DIAGNOSTIC, NORMAL_ADDRESS, ai, 16, can_msg);

	uint8_t ret = tp_request(0, msg);

	//uint8_t ret = tp_request(1, msg1);

	if (ret)
	{
		printf("error code:%d \r\n",ret);
	}

	while (1)
	{
		tp_task();
		Sleep(1);
	}
	system("pause");
	return 0;
}