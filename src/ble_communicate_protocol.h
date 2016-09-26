
#ifndef __COMMUNICATE_PROTOCOL_H__
#define __COMMUNICATE_PROTOCOL_H__

#include "stdint.h"

/******************* Macro defination *************************************/
#define L1_HEADER_MAGIC  (0xAB)     /*header magic number */
#define L1_HEADER_VERSION (0x00)     /*protocol version */
#define L1_HEADER_SIZE   (8)      /*L1 header length*/

/**************************************************************************
* define L1 header byte order
***************************************************************************/
#define L1_HEADER_MAGIC_POS             (0)
#define L1_HEADER_PROTOCOL_VERSION_POS  (1)
#define L1_PAYLOAD_LENGTH_HIGH_BYTE_POS (2)         /* L1 payload lengh high byte */
#define L1_PAYLOAD_LENGTH_LOW_BYTE_POS  (3)
#define L1_HEADER_CRC16_HIGH_BYTE_POS   (4)
#define L1_HEADER_CRC16_LOW_BYTE_POS    (5)
#define L1_HEADER_SEQ_ID_HIGH_BYTE_POS  (6)
#define L1_HEADER_SEQ_ID_LOW_BYTE_POS   (7)


/********************************************************************************
* define version response
*********************************************************************************/
typedef enum {
    DATA_PACKAGE = 0,
    RESPONSE_PACKAGE =  1,
}L1_PACKAGE_TYPE;

/********************************************************************************
* define ack or nak
*********************************************************************************/
typedef enum {
    ACK = 0,
    NAK = 1,
}L1_ERROR_FLAG;

/*******************************************************************************
* debvice loss alert level
********************************************************************************/
typedef enum {
    NO_ALERT = 0,
    MIDDLE_ALERT = 1,
    HIGH_ALERT = 2
} DEV_LOSS_ALERT_LEVEL;

#define L2_HEADER_SIZE   (2)      /*L2 header length*/
#define L2_HEADER_VERSION (0x00)     /*L2 header version*/
#define L2_KEY_SIZE         (1)
#define L2_PAYLOAD_HEADER_SIZE (3)        /*L2 payload header*/
#define L2_FIRST_VALUE_POS (L2_HEADER_SIZE + L2_PAYLOAD_HEADER_SIZE)
/*****************************************/
#define L1L2_HEAD_LEN  (5)
#define SPORT_HEAD_LEN (L1L2_HEAD_LEN+4)
#define SPOTSMODE_POS (12)
/*****************************************/

#define GLOBAL_RESPONSE_BUFFER_SIZE 504  //chaokw

/*********************************************************************
* individual response buffer
**********************************************************************/
struct Response_Buff_Type_t
{
    uint16_t sequence_id;
    uint8_t  check_success;
    uint8_t  isUsed;
};

#define MAX_SEND_TASK  (1)  
#define SEND_RETRY_TIMES (10)  

/* L1 header struct */
typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint16_t payload_len;
    uint16_t crc16;
    uint16_t sequence_id;
}
L1Header_t;

/*L1 version defination */
typedef struct
{
uint8_t version  :
    4;
uint8_t ack_flag  :
    1;
uint8_t err_flag  :
    1;
uint8_t reserve  :
    2;
}
L1_version_def_t;

typedef union{
    L1_version_def_t version_def;
    uint8_t value;
}L1_version_value_t;

enum crc_check_result
{
    CRC_SUCCESS = 0,
    CRC_FAIL = 1
};

typedef struct
{
    uint8_t should_alert;              /* control should alert or not */
    uint8_t alert_level;
}
dev_loss_control_t;

typedef union {
    uint16_t data;
    dev_loss_control_t controller;
}dev_loss_control;

typedef struct
{
uint16_t v_length  :
    9;
uint16_t reserve  :
    7;
}
Key_Header_t;
typedef union
{
    uint16_t data;
    Key_Header_t key_header_bit_field;
} Key_Header_u;

typedef struct
{
    uint8_t cmd_ID;
    uint8_t version;
    uint8_t key;
    Key_Header_u key_header;
}
L2DataHeader_t;


/* Command ID */   //chaokw
typedef enum {
    SET_AP_CLIENT_CMD_ID = 0x01,
    RSP_AP_CLIENT_CMD_ID = 0x05,		
    GET_DEVICE_ID_CMD_ID = 0x02,
    GET_SITE_SURVEY_CMD_ID = 0x03,
    SET_WIFI_PARA_CMD_ID = 0x04,

	
    TEST_COMMAND_ID = 0xFF
}BLUETOOTH_COMMUNICATE_COMMAND;



typedef enum  {
    KEY_WIFI_SSID   = 0x01,
    KEY_WIFI_PASSWORD  = 0x02,
    KEY_DEVICE_ID  = 0x03,
    KEY_AP_MAC = 0x04,
    KEY_AP_IP = 0x05,
    KEY_STATUS = 0x06
}BLUETOOTH_COMMUNICATE_KEY;


typedef enum  {
    SEND_SUCCESS = 1,
    SEND_FAIL   = 0,
}SEND_STATUS;

typedef enum {
    WAIT_START = 0,
    WAIT_MESSAGE,
    MESSAGE_RESOLVE
}RECEIVE_STATE;

typedef void (*send_status_callback_t)(SEND_STATUS status );


/****************************************************************
* sending Buffer use state
*****************************************************************/
typedef struct
{
uint8_t isUsed      :
    1;
uint8_t TxComplete  :
    1;
uint8_t GetResPack  :
    1;
uint8_t reserved    :
    5;
}
SendingBufferUseState_t;

typedef union {
    uint8_t data;
    SendingBufferUseState_t usingState;
}SendingBufferUseState;

/*Used by L1, used to send whole L1 package */
typedef struct
{
    uint8_t *         content;   /* content to be send */
    send_status_callback_t                   callback;   /* send status callback */
    uint16_t          length;    /* content length */
    uint16_t             contentLeft;     /* content left for send*/
    uint16_t         sequence_id;        /* sequence id for this package*/
    uint8_t                         isUsed;    /* is the current struct used*/
    uint8_t             resendCount;     /* whole L1 package resend count */
}
L1_Send_Content;

/*used by L2, To describe the L2 content to be send */
typedef struct
{
    uint8_t *         content;   /* content to be send */
    send_status_callback_t   callback;   /* send status callback */
    uint16_t          length;    /* content length */
}
L2_Send_Content;

typedef enum {
    CONTENT_NONE = 0,
    CONTENT_HEADER = 1,
    CONTENT_DATA = 2,
    CONTENT_ACK = 3
}SEND_CONTENT_TYPE_T;

/***********************************************************************
* This enum describe the current task type
************************************************************************/
typedef enum {
    TASK_NONE = 0,
    TASK_DATA = 1,
    TASK_ACK = 2
}SEND_TASK_TYPE_T;

typedef struct
{
    L1_Send_Content * content;
    uint16_t        isUsed;
}
L1_Header_Schedule_type_t;


/* Definend for send callback */
typedef void (*send_complete_callback_t)(void *context,SEND_TASK_TYPE_T type);

typedef  struct
{
    send_complete_callback_t callback;
    void * context;
    SEND_TASK_TYPE_T task_type;
}
SendCompletePara;


/******************* Function defination **********************************/

void L1_receive_data(uint8_t * data, uint16_t length);
uint32_t ble_timer_init(void);



#endif //__COMMUNICATE_PROTOCOL_H__
