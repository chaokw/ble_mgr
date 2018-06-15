/* --- system includes ---*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/un.h>
#include <poll.h>
#include <assert.h>
#include <linux/if.h>
#include <linux/types.h>
#include <linux/wireless.h>

/* --- project includes ---*/
#include "ap_client.h"

survey_table site[64];
int survey_count = 0;
static unsigned char timeout_indicate = 0;
#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#define RTPRIV_IOCTL_SET (SIOCIWFIRSTPRIV + 0x02)
#define RTPRIV_IOCTL_GSITESURVEY (SIOCIWFIRSTPRIV + 0x0D)

/***********************************************************************
* redefine iwpriv func
************************************************************************/
static void iwpriv(const char *name, const char *key, const char *val)
{
	int socket_id;
	struct iwreq wrq;
	char data[64];

	snprintf(data, 64, "%s=%s", key, val);
	socket_id = socket(AF_INET, SOCK_DGRAM, 0);
	strcpy(wrq.ifr_ifrn.ifrn_name, name);
	wrq.u.data.length = strlen(data);
	wrq.u.data.pointer = data;
	wrq.u.data.flags = 0;
	ioctl(socket_id, RTPRIV_IOCTL_SET, &wrq);
	close(socket_id);
}


/***********************************************************************
* skip next field from result
************************************************************************/
static void next_field(char **line, char *output, int n) {
	char *l = *line;
	int i;

	memcpy(output, *line, n);
	*line = &l[n];
	for (i = n - 1; i > 0; i--) {
		if (output[i] != ' ')
			break;
		output[i] = '\0';
	}
}


/***********************************************************************
* seach all nearby ap ssid by site survey ioctl
************************************************************************/
void wifi_site_survey(const char *ifname, char* essid, int print)
{
	char *scan = malloc(IW_SCAN_MAX_DATA);
	int ret;
	int socket_id;
	struct iwreq wrq;
	char *line, *start;

	iwpriv(ifname, "SiteSurvey", (essid ? essid : ""));
	sleep(5);
	memset(scan, 0x00, IW_SCAN_MAX_DATA);
	strcpy(wrq.ifr_name, ifname);
	wrq.u.data.length = IW_SCAN_MAX_DATA;
	wrq.u.data.pointer = scan;
	wrq.u.data.flags = 0;
	socket_id = socket(AF_INET, SOCK_DGRAM, 0);
	ret = ioctl(socket_id, RTPRIV_IOCTL_GSITESURVEY, &wrq);
	close(socket_id);
	if (ret != 0)
		goto out;
	if (wrq.u.data.length < 1)
		goto out;

	start = scan;
	while (*start == '\n')
		start++;

	line = strtok((char *)start, "\n");
	line = strtok(NULL, "\n");
	survey_count = 0;
	while(line && (survey_count < 64)) {
		next_field(&line, site[survey_count].channel, sizeof(site->channel));
		next_field(&line, site[survey_count].ssid, sizeof(site->ssid));
		next_field(&line, site[survey_count].bssid, sizeof(site->bssid));
		next_field(&line, site[survey_count].security, sizeof(site->security));
		line = strtok(NULL, "\n");
		site[survey_count].crypto = strstr(site[survey_count].security, "/");
		if (site[survey_count].crypto) {
			*site[survey_count].crypto = '\0';
			site[survey_count].crypto++;
			printf("found network - %s %s %s %s\n",
				site[survey_count].channel, site[survey_count].ssid, site[survey_count].bssid, site[survey_count].security);
		} else {
			site[survey_count].crypto = "";
		}
		survey_count++;
	}
	if (survey_count == 0 && !print)
		printf("no results");
out:
	free(scan);
}

/***********************************************************************
* find expected ap ssid from the result of site survey
************************************************************************/
static struct survey_table* wifi_find_ap(const char *name)
{
	int i;
	for (i = 0; i < survey_count; i++)
		if (!strcmp(name, (char*)site[i].ssid))
			return &site[i];
	return 0;
}


/***********************************************************************
* do wifi repeater using known parameters
************************************************************************/
static void wifi_repeater_start(const char *ifname, const char *staname, const char *channel, const char *ssid,
				const char *key, const char *enc, const char *crypto)
{
	char buf[100];
	int enctype = 0;

	snprintf(buf, lengthof(buf) - 1, "ifconfig '%s' up", staname);
	system(buf);

	iwpriv(ifname, "Channel", channel);
	iwpriv(staname, "ApCliEnable", "0");
	if ((strstr(enc, "WPA2PSK") || strstr(enc, "WPAPSKWPA2PSK")) && key) {
		enctype = 1;
		iwpriv(staname, "ApCliAuthMode", "WPA2PSK");
	} else if (strstr(enc, "WPAPSK") && key) {
		enctype = 1;
		iwpriv(staname, "ApCliAuthMode", "WPAPSK");
	} else if (strstr(enc, "WEP") && key) {
		iwpriv(staname, "ApCliAuthMode", "AUTOWEP");
		iwpriv(staname, "ApCliEncrypType", "WEP");
		iwpriv(staname, "ApCliDefaultKeyID", "1");
		iwpriv(staname, "ApCliKey1", key);
		iwpriv(staname, "ApCliSsid", ssid);
	} else if (!key || key[0] == '\0') {
		iwpriv(staname, "ApCliAuthMode", "NONE");
		iwpriv(staname, "ApCliSsid", ssid);
	} else {
		return;
	}
	if (enctype) {
		if (strstr(crypto, "AES") || strstr(crypto, "TKIPAES"))
			iwpriv(staname, "ApCliEncrypType", "AES");
		else
			iwpriv(staname, "ApCliEncrypType", "TKIP");
		iwpriv(staname, "ApCliSsid", ssid);
		iwpriv(staname, "ApCliWPAPSK", key);
	}

       system("uci set dhcp.lan.ignore=1");
       system("ubus call network.interface.lan down");
       system("ubus call network.interface.lan up");
	
       iwpriv(staname, "ApCliEnable", "1");
       memset(buf, 0, 100);
       snprintf(buf, lengthof(buf) - 1, "brctl addif br-lan '%s'", staname);
       system(buf);	
}


/***********************************************************************
* check if apcli0 interface already associate with an ap
************************************************************************/
int check_assoc(char *ifname)
{
	int socket_id, i;
	struct iwreq wrq;

	socket_id = socket(AF_INET, SOCK_DGRAM, 0);
	strcpy(wrq.ifr_ifrn.ifrn_name, ifname);
	ioctl(socket_id, SIOCGIWAP, &wrq);
	close(socket_id);

	for (i = 0; i < 6; i++)
		if(wrq.u.ap_addr.sa_data[i])
			return 1;
	return 0;
}


/***********************************************************************
* when timeout signal arrived, get out of the assoc loop
************************************************************************/
void assoc_timeout(union sigval v)
{
       timeout_indicate = 1;
       printf("assoc_timeout function! %d\n", v.sival_int);
	   
}
   

/***********************************************************************
* main assoc loop to do repeater
************************************************************************/
int assoc_loop(char *ifname, char *staname, char *essid, char *pass)
{
	static int try_count = 0;
	static int assoc_count = 0;

	timer_t timerid;
	struct sigevent evp;
	struct itimerspec tick;
	   
	memset(&evp, 0, sizeof(struct sigevent));   

	evp.sigev_value.sival_int = getpid(); 
	evp.sigev_notify = SIGEV_THREAD;
	evp.sigev_notify_function = assoc_timeout;

	timeout_indicate = 0;
	
       if (timer_create(CLOCK_REALTIME, &evp, &timerid) == -1)
       {
           perror("fail to create timer\r\n");
           return 1;		   
       }

	tick.it_interval.tv_sec = 0;
       tick.it_interval.tv_nsec = 0;
       tick.it_value.tv_sec = 15;
       tick.it_value.tv_nsec = 0;

       if (timer_settime(timerid, 0, &tick, NULL) == -1)
       {
           perror("fail to set timer\r\n");
	    return 1;	   
       }

	/*  firstly should write config ssid/password to uci ------------*/   

	//-----------------------------------------------------
	
	while (1) {

		/*  if timer time out , just beak the loop and return error */
		//printf("assoc_count=%d, try_count=%d", assoc_count, try_count);
              if (timeout_indicate == 1){
			//printf("associate timeout!\r\n");
                     break;
		}
		
		if (!check_assoc(staname)) {
			struct survey_table *site;

			printf("%s is not associated\n", staname);
			printf("scanning for networks...\n");
			wifi_site_survey(ifname, essid, 0);
			site = wifi_find_ap(essid);
			try_count++;
			assoc_count = 0;
			if (site) {
				printf("found network, trying to associate (essid: %s, bssid: %s, channel: %s, enc: %s, crypto: %s)\n",
					essid, site->ssid, site->channel, site->security, site->crypto);
				wifi_repeater_start(ifname, staname, site->channel, essid, pass, site->security, site->crypto);
			} else {
				printf("no signal found to connect to\n");
				try_count = 0;
			}
		} else {
			if (assoc_count == 0) {
				printf("%s is associated\n", staname);
				return 0;
			}
			assoc_count++;
			if (assoc_count > 1)
				try_count = 0;
		}
		sleep(8);
	}
	printf("ap client associate time out\r\n");
	return 1;
}
