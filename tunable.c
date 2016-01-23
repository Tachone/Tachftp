#include "tunable.h"

/*配置文件中不配置，哪些配置项也是有默认值的修改在*/

int tunable_pasv_enable = 1;
int tunable_port_enable = 1;
unsigned int tunable_listen_port = 21;
unsigned int tunable_max_clients = 2000;
unsigned int tunable_max_per_ip = 50;
unsigned int tunable_accept_timeout = 60;
unsigned int tunable_connect_timeout = 60;
unsigned int tunable_idle_session_timeout = 300;
unsigned int tunable_data_connection_timeout = 300;
unsigned int tunable_local_umask = 077;
unsigned int tunable_upload_max_rate = 0;
unsigned int tunable_download_max_rate = 0;
const char *tunable_listen_address;
