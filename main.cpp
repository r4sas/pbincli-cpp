#include <fstream>
#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <jansson.h>
#include <curl/curl.h>

#include "main.h"

CURL *curl;

bool opt_debug = false;
bool opt_notext = false;
bool opt_nocertcheck = false;

const char *opt_server = "https://paste.i2pd.xyz/";
char *opt_proxy;
long opt_proxy_type;
static json_t *opt_config;

struct upload_buffer {
	const void	*buf;
	size_t		len;
	size_t		pos;
};

enum paste_command {
	PASTE_SEND = 1,
	PASTE_GET,
	PASTE_DELETE
};

int paste_mode = 0;

std::string paste_message;
char *paste_filepath;
char *paste_password;

const char *paste_expire = "1day";
bool paste_burn = false;
bool paste_discus = false;

const char *paste_format = "plaintext";

char *paste_id;
char *paste_token;

static char const usage[] = "\
Usage: " PROGRAM_NAME " [OPTIONS] <COMMAND> [paste_id | paste_id#paste_key]\n\
Available commands:\n\
  send                  Send paste\n\
  get                   Receive paste\n\
  delete                Delete paste\n\
\n\
Global options:\n\
  -s, --server=[PROTOCOL://]HOST[:PORT]/[PATH/]\n\
                        Change used server address\n\
  -x, --proxy=[PROTOCOL://]HOST[:PORT]\n\
                        connect through a proxy\n\
  --no-check-certificate\n\
                        do not verify server certificate\n\
  -d, --debug           enable debugging output\n\
  -c, --config=FILE     load a JSON-format configuration file\n\
  -V, --version         display version information and exit\n\
  -h, --help            display this help text and exit\n\
\n\
'send' command options:\n\
" PROGRAM_NAME " [-m text] [...] send\n\
  -m, --message=<TEXT>  text to add in paste written in quotes.\n\
                        If not set, text will be read from stdin\n\
  -f, --file=<FILE>     path to file to attach to paste\n\
                        example: /home/user/document.pdf\n\
  -p  --password=<PASSWORD>\n\
                        password for encrypting paste\n\
  -E  --expire=<5min|10min|1hour|1day|1week|1month|1year|never>\n\
                        paste lifetime (default: 1day)\n\
  -B  --burn            burn sent paste after reading\n\
  -D  --discus          open discussion for sent paste\n\
  -F  --format=<plaintext|syntaxhighlighting|markdown>\n\
                        format of text (default: plaintext)\n\
  -q  --notext          disable text store for paste. If used,\n\
                        --file will be required to be selected!\n\
  -C  --compression=<zlib|none>\n\
                        paste compression mode (default: zlib)\n\
\n\
'get ' command options:\n\
" PROGRAM_NAME " [-p password] get <paste_id#paste_key>\n\
  -p  --password=pass   password for decrypting paste\n\
  paste_id#paste_key    id and key pair of paste\n\
\n\
'delete' command options:\n\
" PROGRAM_NAME " <-t token> delete <paste_id>\n\
  -t  --token=<TOKEN>   password for decrypting paste\n\
  paste_id              id and key pair of paste\n\
";

static char const short_options[] =
	"m:f:p:E:BDF:qC:t:s:x:dc:Vh";

static struct option options[] = {
	{ "message",				required_argument,	NULL, 'm' },
	{ "file",					required_argument,	NULL, 'f' },
	{ "password",				required_argument,	NULL, 'p' },
	{ "expire",					required_argument,	NULL, 'E' },
	{ "burn",					no_argument,		NULL, 'B' },
	{ "discus",					no_argument,		NULL, 'D' },
	{ "format",					required_argument,	NULL, 'F' },
	{ "notext",					no_argument,		NULL, 'q' },
	{ "compression",			required_argument,	NULL, 'C' },
	{ "token",					required_argument,	NULL, 't' },
	{ "paste",					required_argument,	NULL, 1000 },
	{ "no-check-certificate",	no_argument,		NULL, 1001 },
	{ "host",					required_argument,	NULL, 's' },
	{ "proxy",					required_argument,	NULL, 'x' },
	{ "debug",					no_argument,		NULL, 'd' },
	{ "config",					required_argument,	NULL, 'c' },
	{ "version",				no_argument,		NULL, 'V' },
	{ "help",					no_argument,		NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

static void show_version_and_exit(void)
{
	printf("%s v%s\n"
		"%s\n",
		PACKAGE_NAME, PACKAGE_VERSION,
		curl_version());
	exit(EXIT_SUCCESS);
}

static void show_usage_and_exit(int status)
{
	if (status)
		fprintf(stderr, "Try `" PROGRAM_NAME " --help' for more information.\n");
	else
		printf(usage);

	exit(status);
}

void parse_arg(int key, char *arg)
{
	switch(key) {
		case 'm':
			paste_message = strdup(arg);
			break;

		case 'f':
			free(paste_filepath);
			paste_filepath = strdup(arg);
			break;

		case 'p':
			free(paste_password);
			paste_password = strdup(arg);
			break;

		case 'E':
			// here provided only default values
			if (!strcasecmp(arg, "5min") || !strcasecmp(arg, "10min") || !strcasecmp(arg, "1hour") || !strcasecmp(arg, "1day") ||
				!strcasecmp(arg, "1week") || !strcasecmp(arg, "1month") || !strcasecmp(arg, "1year") || !strcasecmp(arg, "never"))
				paste_expire = strdup(arg);
			break;

		case 'B':
			paste_burn = true;
			break;

		case 'D':
			paste_discus = true;
			break;

		case 'F':
			// here provided only default values
			if (!strcasecmp(arg, "plaintext") || !strcasecmp(arg, "syntaxhighlighting") || !strcasecmp(arg, "markdown"))
				paste_format = strdup(arg);
			break;

		case 'q':
			opt_notext = true;
			break;

		case 1000:
			free(paste_id);
			paste_id = strdup(arg);
			break;

		case 't':
			free(paste_token);
			paste_token = strdup(arg);
			break;

		case 1001:
			opt_nocertcheck = true;
			break;

		case 's':
		opt_server = strdup(arg);
			break;

		case 'x':
			if (!strncasecmp(arg, "socks4://", 9))
				opt_proxy_type = CURLPROXY_SOCKS4;
			else if (!strncasecmp(arg, "socks5://", 9))
				opt_proxy_type = CURLPROXY_SOCKS5;
#if LIBCURL_VERSION_NUM >= 0x071200
			else if (!strncasecmp(arg, "socks4a://", 10))
				opt_proxy_type = CURLPROXY_SOCKS4A;
			else if (!strncasecmp(arg, "socks5h://", 10))
				opt_proxy_type = CURLPROXY_SOCKS5_HOSTNAME;
#endif
			else
				opt_proxy_type = CURLPROXY_HTTP;
			free(opt_proxy);
			opt_proxy = strdup(arg);
			break;

		case 'c':
			json_error_t err;
			if (opt_config) {
				json_decref(opt_config);
				opt_config = NULL;
			}

			opt_config = JSON_LOADF(arg, &err);

			if (!json_is_object(opt_config)) {
				fprintf(stderr, "JSON decode of %s failed", arg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'd':
			opt_debug = true;
			break;

		case 'V':
			show_version_and_exit();
			break;

		case 'h':
			show_usage_and_exit(EXIT_SUCCESS);
			break;

		default:
			show_usage_and_exit(EXIT_FAILURE);
			break;
	}
}

void parse_config(json_t* json_obj)
{
	int i;
	json_t *val;

	if (!json_is_object(json_obj))
		return;

	for (i = 0; i < ARRAY_SIZE(options); i++) {

		if (!options[i].name)
			break;

		if (!strcasecmp(options[i].name, "config"))
			continue;

		val = json_object_get(json_obj, options[i].name);
		if (!val)
			continue;

		if (options[i].has_arg && json_is_string(val)) {
			char *s = strdup(json_string_value(val));
			if (!s)
				continue;
			parse_arg(options[i].val, s);
			free(s);
		}
		else if (options[i].has_arg && json_is_integer(val)) {
			char buf[16];
			sprintf(buf, "%d", (int) json_integer_value(val));
			parse_arg(options[i].val, buf);
		}
		else if (options[i].has_arg && json_is_real(val)) {
			char buf[16];
			sprintf(buf, "%f", json_real_value(val));
			parse_arg(options[i].val, buf);
		}
		else if (!options[i].has_arg) {
			if (json_is_true(val))
				parse_arg(options[i].val, (char*) "");
		}
		else
			fprintf(stderr, "JSON option %s invalid", options[i].name);
	}
}

size_t all_data_cb(char *contents, size_t size, size_t nmemb, void *userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

static json_t *json_server_call(CURL *curl, const char *url, const char *req, int *curl_err)
{
	json_t *val, *err_val, *res_val;
	int rc;
	std::string all_data;
	struct upload_buffer upload_data;
	json_error_t err;
	struct curl_slist *headers = NULL;
	char *httpdata;
	char len_hdr[64];
	char curl_err_str[CURL_ERROR_SIZE] = { 0 };

	/* it is assumed that 'curl' is freshly [re]initialized at this pt */

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (opt_nocertcheck) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	}
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	if (opt_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, opt_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, opt_proxy_type);
	}

	switch (paste_mode) {
		case PASTE_SEND:
		case PASTE_DELETE:
			curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
			curl_easy_setopt(curl, CURLOPT_POST, 1);
			upload_data.buf = req;
			upload_data.len = strlen(req);
			upload_data.pos = 0;
			sprintf(len_hdr, "Content-Length: %lu", (unsigned long) upload_data.len);
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, len_hdr);
			break;
	}

	headers = curl_slist_append(headers, "X-Requested-With: JSONHttpRequest");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	rc = curl_easy_perform(curl);
	if (curl_err != NULL)
		*curl_err = rc;
	if (rc != CURLE_OK) {
		if (rc != CURLE_OPERATION_TIMEDOUT) {
			fprintf(stderr, "HTTP request failed: %s", curl_err_str);
			goto err_out;
		}
	}

	if (!all_data.length()) {
		fprintf(stderr, "Empty data received in json_rpc_call.");
		goto err_out;
	}

	httpdata = (char*) all_data.c_str();

	std::cout << httpdata << std::endl;

	val = JSON_LOADS(httpdata, &err);
	if (!val) {
		fprintf(stderr, "JSON decode failed(%d): %s", err.line, err.text);
		goto err_out;
	}

	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	return val;

err_out:
	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	return NULL;
}

static void parse_cmdline(int argc, char *argv[])
{
	int key;

	while ((key = getopt_long(argc, argv, short_options, options, NULL)) != -1) {
		parse_arg(key, optarg);
	}

	parse_config(opt_config);
}

static void initialize_curl()
{
	long flags;

	// cURL initialization
	flags = strncmp(opt_server, "https:", 6)
		? (CURL_GLOBAL_ALL & ~CURL_GLOBAL_SSL)
		: CURL_GLOBAL_ALL;
	if (curl_global_init(flags)) {
		fprintf(stderr, "CURL initialization failed");
		exit(EXIT_FAILURE);
	} else {
		curl = curl_easy_init();
		if (unlikely(!curl)) {
			fprintf(stderr, "CURL initialization failed");
			exit(EXIT_FAILURE);
		}
	}
}

// ### main ###

int main (int argc, char *argv[])
{
	json_t *resp = NULL;
	int err = 0;

	parse_cmdline(argc, argv);

	if (optind >= argc) {
		fprintf(stderr, "%s: command not specified! (see --help)\n", PROGRAM_NAME);
		return EXIT_FAILURE;
	}

	// Program mode changer
	if (!strcasecmp(argv[optind], "send"))
		paste_mode = PASTE_SEND;
	else if (!strcasecmp(argv[optind], "get"))
		paste_mode = PASTE_GET;
	else if (!strcasecmp(argv[optind], "delete"))
		paste_mode = PASTE_DELETE;
	else {
		fprintf(stderr, "%s: unknown command specified! (see --help)\n", PROGRAM_NAME);
		return EXIT_FAILURE;
	}
	// increase options index for additional info
	optind++;

	printf("===== %s =====\n", PROGRAM_NAME);

	if (paste_mode == PASTE_SEND) {
		if(paste_message.length()) {
			printf("Received text: %s\n", paste_message.c_str());
		} else if(!opt_notext) {
			printf("No text is received, so reading it from stdin...\n");
			while (std::getline(std::cin, paste_message)){ }
			printf("Received text: %s\n", paste_message.c_str());
		}

		printf("Format: %s", paste_format);

	} else if (paste_mode == PASTE_GET) {
		CURLU *h;
		CURLUcode uc;

		std::string pasteData;
		std::string pasteID;
		std::string pasteKey;
		std::string pasteUrl;

		// reading paste id and key from argv
		if (optind >= argc) { // if paste info is not specified
			fprintf(stderr, "%s: paste information is not provided! (see --help)\n", PROGRAM_NAME);
			return EXIT_FAILURE;
		} else 
			pasteData = strdup(argv[optind]);

		// validate that received value is not full URL
		h = curl_url(); /* get a handle to work with */ 
		if(!h)
			return EXIT_FAILURE;

		/* parse a full URL */ 
		uc = curl_url_set(h, CURLUPART_URL, pasteData.c_str(), 0);
		if(uc) // if that is not URL
		{
			std::cout << "That is not URL" << std::endl;

			size_t pos = pasteData.find('#');
			if (pos != std::string::npos) {
				pasteID = pasteData.substr(0, pos++);
				pasteKey = pasteData.substr(pos);
			} else {
				fprintf(stderr, "%s: received paste information is not looks like needed! (see --help)\n", PROGRAM_NAME);
				return EXIT_FAILURE;
			}

			pasteUrl = std::string(opt_server) + "?" + std::string(pasteID);
			std::cout << "At the end we have URL \"" << pasteUrl << "\" with key " << pasteKey << std::endl;

		} else {
			std::cout << "This is URL" << std::endl;

			size_t pos = pasteData.find('#');
			if (pos != std::string::npos) {
				pasteUrl = pasteData.substr(0, pos++);
				pasteKey = pasteData.substr(pos);

				size_t pos = pasteData.find('?');
				if (pos != std::string::npos) {
					opt_server = pasteData.substr(0, pos++).c_str();
				} else {
					fprintf(stderr, "%s: can't find a request option separator \"?\" in URL! (see --help)\n", PROGRAM_NAME);
					return EXIT_FAILURE;
				}
			} else {
				fprintf(stderr, "%s: can't find a \"#\" delimiter in URL! (see --help)\n", PROGRAM_NAME);
				return EXIT_FAILURE;
			}
		}
		curl_url_cleanup(h);

		initialize_curl();

		std::cout << "Requesting data from " << pasteUrl << "...\n";
		resp = json_server_call(curl, pasteUrl.c_str(), NULL, &err);
		std::cout << resp;
	} else if (paste_mode == PASTE_DELETE) {
		// todo
	}
}