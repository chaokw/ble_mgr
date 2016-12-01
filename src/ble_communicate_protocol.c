/* --- system includes ---*/
#include "stdint.h"
#include "string.h"
#include "stdio.h"

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

/* --- project includes ---*/
#include "msgq.h"
#include "ble_crc16.h"
#include "ble_communicate_protocol.h"
#include "ble_mgr.h"
#include "ap_client.h"

/****************************************************************************
* Tempory send buffer
*****************************************************************************/
uint8_t global_reponse_buffer[GLOBAL_RESPONSE_BUFFER_SIZE];
//used to send buffer individually
uint8_t global_L1_header_buffer[L1_HEADER_SIZE];

/**************************************************************************
* L1 send sequence id
***************************************************************************/
uint16_t L1_sequence_id = 0;

/***************************************************************************
* define a single response buffer
* used to store response package triggered while sending
****************************************************************************/
static struct Response_Buff_Type_t g_ack_package_buffer =
{
        0,0,0
};

static L1_Send_Content * g_next_L1_send_content_buffer = NULL;

/***************************************************************************
* This variable is used to deal with : send header immediately after send response
****************************************************************************/
static L1_Header_Schedule_type_t L1_header_need_schedule = {NULL,0};


/***************************************************************************
* static & global varable 
****************************************************************************/
static L1_Send_Content sendContent[MAX_SEND_TASK];
static L1_Send_Content* current_package_wait_response = NULL;

static SEND_TASK_TYPE_T current_task_type = TASK_NONE;
static SEND_TASK_TYPE_T next_task_type = TASK_NONE;

static RECEIVE_STATE receive_state = WAIT_START;
static uint8_t received_buffer[GLOBAL_RECEIVE_BUFFER_SIZE];
static uint16_t received_content_length = 0;
static int16_t length_to_receive;

static uint32_t L1_resend_package(L1_Send_Content * content);
static L1Header_t* construct_response_package(uint16_t sequence_id, Bool check_success);
uint32_t start_resend_timer(void);
uint32_t stop_resend_timer(void);


/***************************************************************************
* extern data
****************************************************************************/
extern BLEMgmtTaskCtxT  *gpBLEMgmtTaskCtx;


/**********************************************************************
* set the callback of send complete 
***********************************************************************/
static SendCompletePara m_send_complete_para =  {NULL, NULL,TASK_NONE};
void set_complete_callback(SendCompletePara para)
{
    m_send_complete_para.callback = para.callback;
    m_send_complete_para.context = para.context;
    m_send_complete_para.task_type = para.task_type;
}

static void delay_send_func(void * context)   // need a timer to start up (should support on/off)
{
    uint32_t error_code;
    //L1_Send_Content * content = (L1_Send_Content *)context;  //chaokw
    L1_Send_Content * content = current_package_wait_response;
	
    SendCompletePara sendPara;

    //content != NULL
    if(content->isUsed == 0) {
        return;
    }

    if(content->contentLeft == 0) {	
        if(content->resendCount < 3) {
            content->resendCount++;
            printf("time out resend\r\n");
            error_code = L1_resend_package(content);
            if(error_code == BLE_OK) {
                return;
            }
        }
    }
    printf("resend more than three times\r\n");

    sendPara.callback = NULL;
    sendPara.context = NULL;
    sendPara.task_type = TASK_NONE;
    set_complete_callback(sendPara); 

    current_task_type = TASK_NONE;
    next_task_type = TASK_NONE;

    content->isUsed = 0; 
    if(content->callback) {
        content->callback(SEND_FAIL);
    }
}


/**********************************************************************
* register wait response package
***********************************************************************/
static void register_wait_response(L1_Send_Content * content)
{
    current_package_wait_response = content;
}


/**********************************************************************
* Whole package resend
***********************************************************************/
static uint32_t L1_resend_package(L1_Send_Content * content)
{
    printf("will resend a package\r\n");

    if(!content) {
        return BLE_INVALIDDATA;
    }
    //fill header
    global_L1_header_buffer[L1_HEADER_MAGIC_POS] = L1_HEADER_MAGIC;
    global_L1_header_buffer[L1_HEADER_PROTOCOL_VERSION_POS] = L1_HEADER_VERSION;
    global_L1_header_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] = (content->length >> 8 & 0xFF);
    global_L1_header_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] = (content->length & 0xFF);
    //cal crc
    uint16_t crc16_ret = ble_crc16(0,content->content,content->length);
    global_L1_header_buffer[L1_HEADER_CRC16_HIGH_BYTE_POS] = ( crc16_ret >> 8) & 0xff;
    global_L1_header_buffer[L1_HEADER_CRC16_LOW_BYTE_POS] = crc16_ret & 0xff;
    //sequence id
    global_L1_header_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] = (L1_sequence_id >> 8) & 0xff;
    global_L1_header_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] = L1_sequence_id & 0xff;
    //prepare for send L2 content
    content->contentLeft = content->length;
    content->sequence_id = L1_sequence_id;
    //every time send a package,increase L1_sequence_id, whether it's success or not
    L1_sequence_id ++;
    //register need to schedule header
    L1_header_need_schedule.isUsed = 1;
    L1_header_need_schedule.content = content;

    //schedule async send
    
    //write(content,TASK_DATA);   //chaokw
    //write(gpBLEMgmtTaskCtx->uart.fd, content, content->length+8);	
    schedule_send(content,TASK_DATA);

    return BLE_OK;
}


/************************************************************************
* If receive response package call this function
*************************************************************************/
static void response_package_handle(uint16_t sequence_id,uint8_t crc_check)
{
    uint32_t err_code;
    SendCompletePara sendPara;
    if(!current_package_wait_response) {
        printf("no package wait response\r\n");
        return;
    }
    if(current_package_wait_response->sequence_id == sequence_id ) {
		
        // get response for current package so stop timer
        stop_resend_timer();  //chaokw
		
        if( crc_check == CRC_SUCCESS) {

            printf("get response show crc success, sequence id is 0x%x\r\n", sequence_id);
            sendPara.callback = NULL;
            sendPara.context = NULL;
            sendPara.task_type = TASK_NONE;
            set_complete_callback(sendPara); 

            current_package_wait_response->isUsed = 0;
            if(current_package_wait_response->callback) {
                current_package_wait_response->callback(SEND_SUCCESS);
            }
        } else { //error resend
            printf("get response show crc fail\r\n");

            if(current_package_wait_response->resendCount >= 3) {

                sendPara.callback = NULL;
                sendPara.context = NULL;
                sendPara.task_type = TASK_NONE;
                set_complete_callback(sendPara); //cancle callback

                current_task_type = TASK_NONE;
                next_task_type = TASK_NONE;
                current_package_wait_response->isUsed = 0;

                if(current_package_wait_response->callback) {
                    current_package_wait_response->callback(SEND_FAIL);
                }
            } else {
                printf("response crc error resend, sequence id is 0x%x\r\n", sequence_id);

                current_package_wait_response->resendCount++;
                err_code = L1_resend_package(current_package_wait_response);
                if(err_code != BLE_OK) {
                    sendPara.callback = NULL;
                    sendPara.context = NULL;
                    sendPara.task_type = TASK_NONE;
                    set_complete_callback(sendPara); //cancle callback

                    current_package_wait_response->isUsed = 0;
                    if(current_package_wait_response->callback) {
                        current_package_wait_response->callback(SEND_FAIL);
                    }
                }
            }
        }
    }
    else {
        printf("receive a package with wrong sequesnce id\r\n");
    }
}


/****************************************************************************
* new_task_type shows where call this function
*****************************************************************************/
void uart_send(void * para,uint16_t event_size,SEND_TASK_TYPE_T new_task_type)
{

    ASSERT(para != NULL);
    ASSERT(event_size == sizeof(void*));

    if(current_task_type == TASK_NONE) {
        current_task_type = new_task_type;
    } else {
        if((current_task_type == TASK_ACK) && (new_task_type == TASK_DATA) && (L1_header_need_schedule.isUsed == 1)) {//get a data send request while no buffer to send ack
            g_next_L1_send_content_buffer = *((L1_Send_Content **)para);
            next_task_type = TASK_DATA;
            return;
        } else if((current_task_type == TASK_DATA) && (new_task_type == TASK_ACK)) {
            next_task_type = TASK_ACK;
            return;
        }
    }

    uint32_t error_code;
    uint16_t sendLen = 0;
    uint8_t * currentSendPointer = NULL;
    SendCompletePara sendPara;
    L1_Send_Content * content = NULL;

    error_code = BLE_OK;

LABEL_SEND_ACK:
    if(current_task_type == TASK_ACK) {
        if(g_ack_package_buffer.isUsed == 1) {
            currentSendPointer = (uint8_t *)construct_response_package(g_ack_package_buffer.sequence_id,(g_ack_package_buffer.check_success == 1) ? TRUE:FALSE );
            sendLen = L1_HEADER_SIZE;


        printf("\r\n------------ack to be send-----------\r\n");
	 int i;
        for(i=0; i<sendLen; i++) {	
	     printf("0x%x ", currentSendPointer[i]);
        }
        printf("\r\n-------------------------------------\r\n");
	     
            error_code = write(gpBLEMgmtTaskCtx->uart.fd, currentSendPointer, sendLen);
        if(error_code > 0){
            error_code = BLE_OK;
	 }

	     if(error_code == BLE_OK){	
                current_task_type = TASK_NONE;
                g_ack_package_buffer.isUsed = 0;
                if((next_task_type == TASK_DATA) && (g_next_L1_send_content_buffer != NULL)) {
                    current_task_type = TASK_DATA;
                    next_task_type = TASK_NONE;
                    content = g_next_L1_send_content_buffer;
                    goto SEND_DATA_LABLE; 
                }
                return;
	     }else{
		  current_task_type = TASK_NONE;
                next_task_type = TASK_NONE;
                g_ack_package_buffer.isUsed= 0;
                if((next_task_type == TASK_DATA) && (g_next_L1_send_content_buffer != NULL)) {
                    current_task_type = TASK_DATA;
                    next_task_type = TASK_NONE;
                    content = g_next_L1_send_content_buffer;
                    goto SEND_DATA_LABLE; 
                }
            }
        }
        return;
    }
	
    if(current_task_type == TASK_NONE) {
        return; 
    }

    content =  *((L1_Send_Content **)para);
    ASSERT(content != NULL);

SEND_DATA_LABLE:
    error_code = BLE_OK;
    SEND_CONTENT_TYPE_T sendContentType = CONTENT_NONE;
    while(error_code == BLE_OK) { 
        if(L1_header_need_schedule.isUsed == 1) {
            currentSendPointer = global_L1_header_buffer;
            sendLen = L1_HEADER_SIZE;
            sendContentType = CONTENT_HEADER;
        }else {
            if(content ->contentLeft != 0) {
                ASSERT(content->content != NULL);
                sendLen = content->contentLeft;
                currentSendPointer = content->content;
                sendContentType = CONTENT_DATA;
            } else {
                sendContentType = CONTENT_NONE;
            }
        }

        //first check if data is send complete
        if(sendContentType == CONTENT_NONE) {
            break;
        }


        printf("\r\n------------data to be send-----------\r\n");
	 int i;
        for(i=0; i<sendLen; i++) {	
	     printf("%x", currentSendPointer[i]);  //chaokw
        }
        printf("\r\n--------------------------------------\r\n");


        if( sendLen <=120){   //chaokw
            error_code = write(gpBLEMgmtTaskCtx->uart.fd, currentSendPointer, sendLen);
        }
	 else{
	     error_code = write(gpBLEMgmtTaskCtx->uart.fd, currentSendPointer, 120);
	     sleep(1);
	     error_code = write(gpBLEMgmtTaskCtx->uart.fd, &currentSendPointer[120], sendLen - 120);
	 } 

        if(error_code > 0){
            error_code = BLE_OK;
	 }

        if (error_code == BLE_OK){
            switch(sendContentType) {
                case CONTENT_NONE:
                    break;
                case CONTENT_HEADER: 
                    if(L1_header_need_schedule.isUsed == 1) {
                        L1_header_need_schedule.isUsed = 0;
                        memset(global_L1_header_buffer,0,L1_HEADER_SIZE);
                    }
                    break;
                case CONTENT_DATA:
                    content ->contentLeft -= sendLen;
                    if(content ->contentLeft == 0) {
                        sendPara.callback = NULL;
                        sendPara.context = NULL;
                        sendPara.task_type = TASK_NONE;
                        set_complete_callback(sendPara);
                        current_task_type = TASK_NONE;
                        //begin to wait package response
                        register_wait_response(content);
                        //start timer wait for response
                        start_resend_timer();
                        if((next_task_type == TASK_ACK) && (g_ack_package_buffer.isUsed == 1)) {
                            current_task_type = TASK_ACK;
                            next_task_type = TASK_NONE;
                            goto LABEL_SEND_ACK;
                        }
                    }
                    break;
                case CONTENT_ACK:
                    if(g_ack_package_buffer.isUsed == 1) { //send ack package
                        current_task_type = TASK_NONE;
                        g_ack_package_buffer.isUsed = 0;
                    }
                    break;
                default:
                    break;
            }
        }else {
            //send fail
            sendPara.callback = NULL;
            sendPara.context = NULL;
            sendPara.task_type = TASK_NONE;
            set_complete_callback(sendPara);
            current_task_type = TASK_NONE;
            if(content->callback) {
                content->isUsed = 0;
                content->callback(SEND_FAIL);
            }
            if(next_task_type == TASK_ACK) {
                current_task_type =  TASK_ACK;
                next_task_type = TASK_NONE;
                goto LABEL_SEND_ACK;
            } else {
                return;
            }
         }	
    	}
}



/**********************************************************************
* Schedule next package to be send after prev package send success
***********************************************************************/
void schedule_send(void * contenxt,SEND_TASK_TYPE_T task)   //chaokw
{

    ASSERT(contenxt != NULL);
    if(task == TASK_DATA) { 
        L1_Send_Content * data_content = (L1_Send_Content *)contenxt;
        //printf("do L1 schedule_send TASK_DATA\n");			
        if(data_content->contentLeft != 0) {
            stop_resend_timer();   //chaokw
        }		
        uart_send(&data_content,sizeof(L1_Send_Content *),TASK_DATA);
    } else if (task == TASK_ACK) {
        struct Response_Buff_Type_t * ack_content = (struct Response_Buff_Type_t *)contenxt;
        //printf("do L1 schedule_send TASK_ACK\n");					
        uart_send(&ack_content,sizeof(struct Response_Buff_Type_t *),TASK_ACK);
    } else {
        printf("call schedule_async_send with wrong para\r\n");
    }
}
	

/*************************************************************
 * L1 send content implementation
**************************************************************/
uint32_t L1_send(L2_Send_Content * content)   //chaokw
{
    uint32_t err_code;
    if(!content) {
        return BLE_INVALIDDATA;
    }
    uint32_t i = 0;
    err_code = BLE_NOMEM;
    for( i = 0; i<MAX_SEND_TASK ; ++i ) {   //just  one  now

        if( sendContent[i].isUsed ) {
            continue;
        } else {
            sendContent[i].isUsed = 1;
            err_code = 0;
            break;
        }
    }
    if(err_code == BLE_NOMEM) {
        return BLE_NOMEM;
    }

    /*fill header*/
    global_L1_header_buffer[L1_HEADER_MAGIC_POS] = L1_HEADER_MAGIC;  
    global_L1_header_buffer[L1_HEADER_PROTOCOL_VERSION_POS] = L1_HEADER_VERSION;     
    global_L1_header_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] = (content->length >> 8 & 0xFF);  
    global_L1_header_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] = (content->length & 0xFF);
    /*cal crc*/
    uint16_t crc16_ret = ble_crc16(0,content->content,content->length);
    global_L1_header_buffer[L1_HEADER_CRC16_HIGH_BYTE_POS] = ( crc16_ret >> 8) & 0xff;
    global_L1_header_buffer[L1_HEADER_CRC16_LOW_BYTE_POS] = crc16_ret & 0xff;
    //sequence id
    global_L1_header_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] = (L1_sequence_id >> 8) & 0xff;
    global_L1_header_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] = L1_sequence_id & 0xff;
    //prepare for send L2 content
    sendContent[i].callback  =  content->callback;
    sendContent[i].content   =  content->content;
    sendContent[i].length  = content->length;
    sendContent[i].contentLeft  = content->length;
    sendContent[i].resendCount = 0;
    sendContent[i].sequence_id = L1_sequence_id;
    //every time send a package,increase L1_sequence_id, whether it's success or not
    L1_sequence_id ++;
    //register need to schedule header
    L1_header_need_schedule.isUsed = 1;
    L1_header_need_schedule.content = &sendContent[i];

    //printf("do L1 send\n");
    //schedule send by writing uart port ttys0
    schedule_send(&sendContent[i],TASK_DATA);

    //write(&sendContent[i],TASK_DATA);
    //write(gpBLEMgmtTaskCtx->uart.fd, &sendContent[i], content->length + 8);	

    return BLE_OK;

}


/***********************************************************************
* parse and resolve L2
************************************************************************/
static uint32_t L2_frame_resolve(uint8_t * data, uint16_t length,RECEIVE_STATE * resolve_state)   //chaokw
{

    //para check
    if((!data) || (length == 0)) {
        return BLE_INVALIDDATA;
    }
    //printf("do L2 frame resolve\n");

    BLUETOOTH_COMMUNICATE_COMMAND command_id;
    uint8_t version_num;
    uint8_t result;	
    uint8_t first_key, second_key;
    uint16_t first_value_length, second_value_length;  

    command_id = (BLUETOOTH_COMMUNICATE_COMMAND)data[0];
    version_num = data[1];
    version_num = version_num;    /*current not use it*/

    printf("command_id: 0x%x\n",command_id);

    switch(command_id) {
        case SET_AP_CLIENT_CMD_ID: 
            first_key = data[2];
            first_value_length = (((data[3]<< 8) |data[4]) & 0x1FF);
	     printf("first_value_length = %d\n", first_value_length);
            char ssid[32] = {0};
	     strncpy(ssid, (char *)&data[L2_FIRST_VALUE_POS], first_value_length);
	     ssid[first_value_length] = '\0';
            printf("parsed ssid is %s\n", ssid);

            second_key = 	data[L2_FIRST_VALUE_POS + first_value_length];
            second_value_length = (((data[L2_FIRST_VALUE_POS + first_value_length+1]<< 8) |data[L2_FIRST_VALUE_POS + first_value_length+2]) & 0x1FF);
            printf("second_value_length = %d\n", second_value_length);			
            char password[32] = {0};
	     strncpy(password, (char *)&data[L2_FIRST_VALUE_POS + first_value_length + L2_PAYLOAD_HEADER_SIZE], second_value_length);
	     password[second_value_length] = '\0';
	     printf("parsed password is %s\n", password);

            result = assoc_loop("ra0", "apcli0", ssid, password);
	     response_apclient_cmd(result);	
	     break;
		 
        case GET_DEVICE_ID_CMD_ID: 
            return_device_id();
            break;

        case GET_SITE_SURVEY_CMD_ID: 
            return_site_survey();			
            break;

        case SET_WIFI_PARA_CMD_ID: 
            set_wifi_para(data);			
            break;
		 
        case TEST_COMMAND_ID: 

            break;
        default:
            break;
    }


    /*resolve complete and restart receive*/
    *resolve_state = WAIT_START;
    return BLE_OK;
}


/******************************************************************************
* direct send response package without any sync op
*******************************************************************************/
static L1Header_t* construct_response_package(uint16_t sequence_id, Bool check_success)
{
    static L1Header_t response_header;
    L1_version_value_t version_ack;


    response_header.magic = L1_HEADER_MAGIC;

    version_ack.version_def.version = L2_HEADER_VERSION;
    version_ack.version_def.ack_flag = 1;
    version_ack.version_def.err_flag = (check_success ? 0 : 1);
    version_ack.version_def.reserve = 0;

    response_header.version =  version_ack.value;
    response_header.payload_len = 0;
    response_header.crc16 = 0;
    response_header.sequence_id = ((sequence_id & 0xFF) << 8) | ((sequence_id >> 8) & 0xFF);

    return &response_header;
}

/*************************************************************************
* L1 receive a package and will send a response
**************************************************************************/
uint32_t L1_receive_response(uint16_t sequence_id, Bool check_success)
{
    //just use the new response request update the older one
    g_ack_package_buffer.check_success = (check_success == TRUE) ? 1 :0;
    g_ack_package_buffer.sequence_id = sequence_id;
    g_ack_package_buffer.isUsed = 1;

    /* uart write  */   //chaokw
    //write(gpBLEMgmtTaskCtx->uart.fd, &g_ack_package_buffer, 8);
    schedule_send(&g_ack_package_buffer,TASK_ACK);

    return BLE_OK;
}


/*************************************************************************
* check the crc16 value for the received package
**************************************************************************/
static uint32_t L1_crc_check(uint16_t crc_value,uint8_t *data,uint16_t length)
{
    uint16_t crc = ble_crc16(0x0000,data,length);
    if(crc == crc_value) {
        return BLE_OK;
    }
    return BLE_INVALIDDATA;
}


/*****************************************************************************
* received content
*****************************************************************************/
void L1_receive_data(uint8_t * data, uint16_t length) 
{

    L1_version_value_t inner_version;
    switch (receive_state) {
        case WAIT_START:
            if(data[0] != L1_HEADER_MAGIC) {
                break;
            }
            received_content_length = 0;
            memcpy(&received_buffer[received_content_length],data,length);
            received_content_length = length;

            length_to_receive = (received_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] | (received_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] << 8)) + L1_HEADER_SIZE;
            length_to_receive -= length;

            if(length_to_receive <= 0) { // just one package
                printf("just one package\n");
                inner_version.value = received_buffer[L1_HEADER_PROTOCOL_VERSION_POS];
                if(inner_version.version_def.ack_flag == RESPONSE_PACKAGE) { //response package
                    printf("receive a ack package\n");
                    receive_state = WAIT_START; //restart receive state machine
                    response_package_handle((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),inner_version.version_def.err_flag);                    
                    return;
                }
                printf("received lenth is %d\n, just one package",received_content_length);
                receive_state = MESSAGE_RESOLVE;
                received_content_length = 0;

                uint16_t crc16_value = (received_buffer[L1_HEADER_CRC16_HIGH_BYTE_POS] << 8 | received_buffer[L1_HEADER_CRC16_LOW_BYTE_POS]);
                if(L1_crc_check(crc16_value,received_buffer+L1_HEADER_SIZE,(received_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] | (received_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] << 8))) == -4) { //check crc for received package //chaokw
                    printf("will send success response\n");   // test phase, disable crc check
                    L1_receive_response((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),TRUE);
		      L2_frame_resolve(received_buffer+L1_HEADER_SIZE,(received_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] | (received_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] << 8)),&receive_state);
                } else { //receive bad package
                    receive_state = WAIT_START;
                    printf("will send crc fail response\n");
                    L1_receive_response((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),FALSE);
                    return;
                }
            } else { // more than one package
                receive_state = WAIT_MESSAGE;
            }
            break;
        case WAIT_MESSAGE:
            memcpy(&received_buffer[received_content_length],data,length);
            received_content_length += length;
            length_to_receive -= length;

            if(length_to_receive <= 0) {
                inner_version.value = received_buffer[L1_HEADER_PROTOCOL_VERSION_POS];
                if(inner_version.version_def.ack_flag == RESPONSE_PACKAGE) { //response package
                    receive_state = WAIT_START; //restart receive state machine
                    response_package_handle((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),inner_version.version_def.err_flag);
                    return;
                }

                printf("received lenth is %d\n",received_content_length);
                receive_state = MESSAGE_RESOLVE;
                received_content_length = 0;

                uint16_t crc16_value = (received_buffer[L1_HEADER_CRC16_HIGH_BYTE_POS] << 8 | received_buffer[L1_HEADER_CRC16_LOW_BYTE_POS]);
                if(L1_crc_check(crc16_value,received_buffer+L1_HEADER_SIZE,(received_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] | (received_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] << 8))) == -4) { //check crc for received package
                    printf("will send success response\n");
                    L1_receive_response((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),TRUE);	      
                    L2_frame_resolve(received_buffer+L1_HEADER_SIZE,(received_buffer[L1_PAYLOAD_LENGTH_LOW_BYTE_POS] | (received_buffer[L1_PAYLOAD_LENGTH_HIGH_BYTE_POS] << 8)),&receive_state);
                } else { //receive bad package
                    //restart receive state machine
                    receive_state = WAIT_START;
                    printf("will send crc fail response\n");
                    L1_receive_response((received_buffer[L1_HEADER_SEQ_ID_LOW_BYTE_POS] | (received_buffer[L1_HEADER_SEQ_ID_HIGH_BYTE_POS] << 8)),FALSE);
                    return;
                }

             } else {
            }
            break;

        case MESSAGE_RESOLVE:
            inner_version.value = data[L1_HEADER_PROTOCOL_VERSION_POS];
            if(inner_version.version_def.ack_flag == RESPONSE_PACKAGE) { //response package
                printf("receive a ack package during MESSAGE_RESOLVE\n");
            }
            break;
        default:
            break;
    }
}


#if 1  //chaokw
/**********************************************************************
* init the environment for private protocol
***********************************************************************/
uint32_t ble_timer_init(void)
{
    uint32_t i = 0;
    uint32_t error_code;

    signal(SIGALRM, delay_send_func);	

    for( ; i< MAX_SEND_TASK; ++i) {
        sendContent[i].isUsed = 0;
        sendContent[i].resendCount = 0;
    }

    return BLE_OK;
}

uint32_t start_resend_timer(void)
{
    alarm(5);
}

uint32_t stop_resend_timer(void)
{
    alarm(0);
}

#endif
