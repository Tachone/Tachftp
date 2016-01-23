#include "common.h"
#include "sysutil.h"
#include "session.h"
#include "str.h"
#include "tunable.h"
#include "parseconf.h"
#include "ftpproto.h"
#include "ftpcodes.h"
#include "hash.h"

/*
typedef struct session
{
	// 控制连接
	char cmdline[MAX_COMMAND_LINE];
	char cmd[MAX_COMMAND];
	char arg[MAX_ARG];

	// 父子进程通道
	int parent_fd;
	int child_fd;
} session_t;
*/

extern session_t *p_sess;
static unsigned int s_children;

static hash_t *s_ip_count_hash;
static hash_t *s_pid_ip_hash;

void check_limits(session_t *sess);
void handle_sigchld(int sig);
unsigned int hash_func(unsigned int buckets, void *key);

unsigned int handle_ip_count(void *ip);
void drop_ip_count(void *ip);

int main(void)
{
	/*
	list_common();
	exit(EXIT_SUCCESS);
	*/

	// 字符串测试
	/*
	char *str1 = "		a b";
	char *str2 = "			  ";

	if (str_all_space(str1))
		printf("str1 all space\n");
	else
		printf("str1 not all space\n");

	if (str_all_space(str2))
		printf("str2 all space\n");
	else
		printf("str2 not all space\n");

	//char *str3 = "abcDef";		// 指针指向一个字符串常量，常量不能被修改
	char str3[] = "abcDef";
	str_upper(str3);
	printf("str3=%s\n", str3);

	long long result = str_to_longlong("12345678901234");
	printf("result = %lld\n", result);


	int n = str_octal_to_uint("711");
	printf("n=%d\n", n);
	*/

	

	parseconf_load_file(MINIFTP_CONF);
	/*必须放到LoadFile的后面---守候进程*/
	daemon(0, 0);

	printf("tunable_pasv_enable=%d\n", tunable_pasv_enable);
	printf("tunable_port_enable=%d\n", tunable_port_enable);

	printf("tunable_listen_port=%u\n", tunable_listen_port);
	printf("tunable_max_clients=%u\n", tunable_max_clients);
	printf("tunable_max_per_ip=%u\n", tunable_max_per_ip);
	printf("tunable_accept_timeout=%u\n", tunable_accept_timeout);
	printf("tunable_connect_timeout=%u\n", tunable_connect_timeout);
	printf("tunable_idle_session_timeout=%u\n", tunable_idle_session_timeout);
	printf("tunable_data_connection_timeout=%u\n", tunable_data_connection_timeout);
	printf("tunable_local_umask=0%o\n", tunable_local_umask);
	printf("tunable_upload_max_rate=%u\n", tunable_upload_max_rate);
	printf("tunable_download_max_rate=%u\n", tunable_download_max_rate);

	if (tunable_listen_address == NULL)
		printf("tunable_listen_address=NULL\n");
	else
		printf("tunable_listen_address=%s\n", tunable_listen_address);


	/*必须用root用户启动，sudo ./miniftpd start*/
	if (getuid() != 0)
	{
		fprintf(stderr, "miniftpd: must be started as root\n");
		exit(EXIT_FAILURE);
	}

/*添加时候更新这段注释--防止初始化时候出错*/
/*
typedef struct session
{
	// 控制连接
	uid_t uid;
	int ctrl_fd;
	char cmdline[MAX_COMMAND_LINE];
	char cmd[MAX_COMMAND];
	char arg[MAX_ARG];

	// 数据连接
	struct sockaddr_in *port_addr;
	int pasv_listen_fd;
	int data_fd;
	int data_process;

	// 限速
	unsigned int bw_upload_rate_max;
	unsigned int bw_download_rate_max;
	long bw_transfer_start_sec;
	long bw_transfer_start_usec;


	// 父子进程通道
	int parent_fd;
	int child_fd;

	// FTP协议状态
	int is_ascii;
	long long restart_pos;
	char *rnfr_name;
	int abor_received;

	// 连接数限制
	unsigned int num_clients;
	unsigned int num_this_ip;
} session_t;
*/

	/*初始化session，一个会话两个进程*/
	session_t sess = 
	{
		/* 控制连接 */
		0, -1, "", "", "",
		/* 数据连接 */
		NULL, -1, -1, 0,
		/* 限速 */
		0, 0, 0, 0,
		/* 父子进程通道 */
		-1, -1,
		/* FTP协议状态 */
		0, 0, NULL, 0,
		/* 连接数限制 */
		0, 0
	};

	p_sess = &sess;

	sess.bw_upload_rate_max = tunable_upload_max_rate;
	sess.bw_download_rate_max = tunable_download_max_rate;

	s_ip_count_hash = hash_alloc(256, hash_func);
	s_pid_ip_hash = hash_alloc(256, hash_func);

	signal(SIGCHLD, handle_sigchld);
	int listenfd = tcp_server(tunable_listen_address, tunable_listen_port);
	int conn;
	pid_t pid;
	struct sockaddr_in addr;

	while (1)
	{
		conn = accept_timeout(listenfd, &addr, 0);
		if (conn == -1)
			ERR_EXIT("accept_tinmeout");

		unsigned int ip = addr.sin_addr.s_addr;
		
		++s_children;
		sess.num_clients = s_children;
		sess.num_this_ip = handle_ip_count(&ip);

		pid = fork();
		if (pid == -1)
		{
			--s_children;
			ERR_EXIT("fork");
		}

		if (pid == 0)
		{
			close(listenfd);
			sess.ctrl_fd = conn;
			check_limits(&sess);
			signal(SIGCHLD, SIG_IGN);
			begin_session(&sess);
		}
		else
		{
			hash_add_entry(s_pid_ip_hash, &pid, sizeof(pid),
				&ip, sizeof(unsigned int));
			
			close(conn);
		}
	}
	return 0;
}

void check_limits(session_t *sess)
{
	if (tunable_max_clients > 0 && sess->num_clients > tunable_max_clients)
	{
		ftp_reply(sess, FTP_TOO_MANY_USERS, 
			"There are too many connected users, please try later.");

		exit(EXIT_FAILURE);
	}

	if (tunable_max_per_ip > 0 && sess->num_this_ip > tunable_max_per_ip)
	{
		ftp_reply(sess, FTP_IP_LIMIT, 
			"There are too many connections from your internet address.");

		exit(EXIT_FAILURE);
	}
}


void handle_sigchld(int sig)
{
	// 当一个客户端退出的时候，那么该客户端对应ip的连接数要减1，
	// 处理过程是这样的，首先是客户端退出的时候，
	// 父进程需要知道这个客户端的ip，这可以通过在s_pid_ip_hash查找得到，
	

	pid_t pid;
	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
	{
		--s_children;
		unsigned int *ip = hash_lookup_entry(s_pid_ip_hash, &pid, sizeof(pid));
		if (ip == NULL)
		{
			continue;
		}

		drop_ip_count(ip);
		hash_free_entry(s_pid_ip_hash, &pid, sizeof(pid));
	}

}

unsigned int hash_func(unsigned int buckets, void *key)
{
	unsigned int *number = (unsigned int*)key;

	return (*number) % buckets;
}

unsigned int handle_ip_count(void *ip)
{
	// 当一个客户登录的时候，要在s_ip_count_hash更新这个表中的对应表项,
	// 即该ip对应的连接数要加1，如果这个表项还不存在，要在表中添加一条记录，
	// 并且将ip对应的连接数置1。

	unsigned int count;
	unsigned int *p_count = (unsigned int *)hash_lookup_entry(s_ip_count_hash,
		ip, sizeof(unsigned int));
	if (p_count == NULL)
	{
		count = 1;
		hash_add_entry(s_ip_count_hash, ip, sizeof(unsigned int),
			&count, sizeof(unsigned int));
	}
	else
	{
		count = *p_count;
		++count;
		*p_count = count;
	}

	return count;
}


void drop_ip_count(void *ip)
{
	// 得到了ip进而我们就可以在s_ip_count_hash表中找到对应的连接数，进而进行减1操作。

	unsigned int count;
	unsigned int *p_count = (unsigned int *)hash_lookup_entry(s_ip_count_hash,
		ip, sizeof(unsigned int));
	if (p_count == NULL)
	{
		return;
	}

	count = *p_count;
	if (count <= 0)
	{
		return;
	}
	--count;
	*p_count = count;

	if (count == 0)
	{
		hash_free_entry(s_ip_count_hash, ip, sizeof(unsigned int));
	}
}
