/* --- system includes ---*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <poll.h>
#include <assert.h>
#include <linux/if.h>
#include <linux/types.h>

/* --- project includes ---*/
#include "msgq.h"
#include "dbgMsgq.h"
#include "ble_mgr.h"
#include "ble_handler.h"
#include "ble_communicate_protocol.h"
#include "ap_client.h"

extern uint8_t global_reponse_buffer[GLOBAL_RESPONSE_BUFFER_SIZE];
extern survey_table site[64];
extern int survey_count;
//extern void wifi_site_survey(const char *ifname, char* essid, int print);

/***********************************************************************
* get mac address via ioctl
************************************************************************/
static int get_mac(char* mac, char *name)
{
    int sockfd;
    struct ifreq tmp;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0) {
        perror("create socket fail\n");
        return -1;
    }

    memset(&tmp,0,sizeof(struct ifreq));
    strncpy(tmp.ifr_name, name, sizeof(tmp.ifr_name)-1);
    if((ioctl(sockfd,SIOCGIFHWADDR,&tmp)) < 0) {
        printf("mac ioctl error\n");
        return -1;
    }
    memcpy(mac, &tmp.ifr_hwaddr.sa_data[0], 6);
    close(sockfd);
    return 0;
}

/***********************************************************************
* get ip address via ioctl
************************************************************************/
static int get_address(char *iface_name, struct in_addr *ip)
{
    int sockfd = -1;
    struct ifreq ifr;
    struct sockaddr_in *addr = NULL;

    memset(&ifr, 0, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, iface_name);
    addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("create socket error!\n");
        return -1;
    }
    if (ioctl(sockfd, SIOCGIFADDR, &ifr) == 0) {
        memcpy(ip, &addr->sin_addr, sizeof(addr->sin_addr));
        close(sockfd);
        return 0;
    }
    close(sockfd);
    return -1;
}


/***********************************************************************
* return device id, define MAC as device id just now
************************************************************************/
void return_device_id(void)
{
	L2_Send_Content sendContent;
	uint8_t ret;
	char mac_addr[6];
	
	global_reponse_buffer[0] = GET_DEVICE_ID_CMD_ID;    /*command id*/
	global_reponse_buffer[1] = L2_HEADER_VERSION;   /*L2 header version */
	global_reponse_buffer[2] = KEY_DEVICE_ID;   /*first key */
	global_reponse_buffer[3] = 0;
	global_reponse_buffer[4] = 6;   /* MAC_LEN  = 6 */

	ret = get_mac(mac_addr, "eth0");
        if(ret < 0) {
                printf("get eth0 mac error\n");
                return;
        }

	memcpy(&global_reponse_buffer[5], mac_addr, 6);

	sendContent.callback  = NULL;
	sendContent.content  = global_reponse_buffer;
	sendContent.length   = L2_HEADER_SIZE + L2_PAYLOAD_HEADER_SIZE + global_reponse_buffer[4]; /*length of whole L2*/

#if 0  //chaokw
        printf("\r\n------L2_Send_Content  to be send------\r\n");
	 int i;
        for(i=0; i<sendContent.length; i++) {	
	     printf("0x%x ", sendContent.content[i]);
        }
        printf("\r\n------------------------------------\r\n");
#endif
	
	L1_send(&sendContent);

	printf("send device id (mac) to ble host\r\n");

}



/***********************************************************************
* return site survey scan result
************************************************************************/
void return_site_survey(void)
{
	L2_Send_Content sendContent;
	uint32_t payload_len = 0;
	uint32_t key_len = 0;
       uint32_t i = 0;

	memset(global_reponse_buffer, 0, GLOBAL_RESPONSE_BUFFER_SIZE);  //200
	global_reponse_buffer[0] = GET_SITE_SURVEY_CMD_ID;  
	global_reponse_buffer[1] = L2_HEADER_VERSION;  
	
	wifi_site_survey("ra0", "", 0);

       for (i=0; i<survey_count; i++){

		if(payload_len > 502){  //502  chaokw
			printf("site survey result data exceed buffer size\r\n");
			break;
		}

		key_len = strlen(site[i].ssid);
		global_reponse_buffer[payload_len + 2] = KEY_WIFI_SSID; 
		global_reponse_buffer[payload_len + 4] = key_len; 
		memcpy(&global_reponse_buffer[payload_len + 5], &site[i].ssid, key_len);
		//printf("num:%d, ssid:%s, len:%d\r\n", i, site[i].ssid, strlen(site[i].ssid));
		payload_len += (3+key_len);
       }

	sendContent.callback  = NULL;
	sendContent.content  = global_reponse_buffer;
	sendContent.length   = L2_HEADER_SIZE + payload_len;

	L1_send(&sendContent);

	printf("send site survey result to ble host\r\n");

}



/***********************************************************************
* response for set ap client command
************************************************************************/
void response_apclient_cmd(int result)
{
	L2_Send_Content sendContent;
	uint8_t ret;
	char mac_addr[6];
	
	global_reponse_buffer[0] = RSP_AP_CLIENT_CMD_ID;
	global_reponse_buffer[1] = L2_HEADER_VERSION;
	global_reponse_buffer[2] = KEY_STATUS;
	global_reponse_buffer[3] = 0;
	global_reponse_buffer[4] = 1;
	global_reponse_buffer[5] = result;

	sendContent.callback  = NULL;
	sendContent.content  = global_reponse_buffer;
	sendContent.length   = L2_HEADER_SIZE + L2_PAYLOAD_HEADER_SIZE + global_reponse_buffer[4];
	
	L1_send(&sendContent);

	printf("response apclient cmd result to ble host\r\n");

}

/***********************************************************************
* set wifi related parameters like ssid/passwd
************************************************************************/
void set_wifi_para(uint8_t * data)
{
	printf("set wifi parameters like ssid/passwd\r\n");
}

