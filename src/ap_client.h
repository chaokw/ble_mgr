#ifndef __APCLIENT_H
#define __APCLIENT_H

typedef struct survey_table
{
	char channel[4];
	char ssid[33];
	char bssid[20];
	char security[23];
	char *crypto;
}survey_table;

int assoc_loop(char *ifname, char *staname, char *essid, char *pass);
void wifi_site_survey(const char *ifname, char* essid, int print);


#endif /* __APCLIENT_H */
