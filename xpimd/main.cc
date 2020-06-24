// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef	XORP_MODULE_NAME
#define XORP_MODULE_NAME	"XPIMD"
#endif
#ifndef XORP_MODULE_VERSION
#define XORP_MODULE_VERSION	"0.1"
#endif

extern "C" {
#include "getopt.h"
#include "command.h"
#include "sigevent.h"
}

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/eventloop.hh"

#include "zebra_router_node.hh"
#include "zebra_mfea_node.hh"
#include "zebra_mld6igmp_node.hh"
#include "zebra_pim_node.hh"

#define XPIMD_VTY_PORT 2610
#define XPIMD_DEFAULT_CONFIG "xpimd.conf"

static bool terminated = false;

extern const char *__progname;

static void usage(int exit_value, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 2, 3)));

/**
 * usage:
 * @exit_value: The exit value of the program.
 *
 * Print the program usage.
 * If @exit_value is 0, the usage will be printed to the standart output,
 * otherwise to the standart error.
 **/
static void
usage(int exit_value, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);

    //
    // If the usage is printed because of error, output to stderr, otherwise
    // output to stdout.
    //
    FILE *output;
    if (exit_value == 0)
	output = stdout;
    else
	output = stderr;

    if (fmt != NULL) {
	vfprintf(output, fmt, ap);
	fprintf(output, "\n\n");
    }

    fprintf(output,
	    "Usage: %s [options...]\n\n", __progname);
    fprintf(output,
	    "Options:\n"
	    "    -d, --daemon       Runs in daemon mode\n"
	    "    -f, --config_file  Set configuration file name\n"
	    "    -i, --pid_file     Set process identifier file name\n"
	    "    -z, --socket       Set path of zebra socket\n"
	    "    -A, --vty_addr     Set vty's bind address\n"
	    "    -P, --vty_port     Set vty's port number\n"
	    "    -u, --user         User to run as\n"
	    "    -g, --group        Group to run as\n"
	    "    -v, --version      Print program version\n"
	    "    -C, --dryrun       Check configuration for validity and exit\n"
	    "    -4, --ipv4         Use IPv4 (default)\n"
	    "    -6, --ipv6         Use IPv6\n"
	    "    -h, --help         Display this help and exit\n");

    exit(exit_value);

    // NOTREACHED
}

static void
sighup(void)
{
    zlog_info("SIGHUP received: ignoring");
}

static void
sigint(void)
{
    XLOG_WARNING("SIGINT received: terminating");
    terminated = true;
}

static void
sigterm(void)
{
    XLOG_WARNING("SIGTERM received: terminating");
    terminated = true;
}

static void
sigusr1(void)
{
    zlog_info("SIGUSR1 received: rotating log");
    zlog_rotate(NULL);
}

static void
multicast_main(bool daemonize, const char *config_file, const char *pid_file,
	       const char *zebra_socket,
	       const char *vty_addr, uint16_t vty_port,
	       const char *user, const char *group, const char *vtysh_path,
	       bool dryrun, int family)
{
    zebra_capabilities_t caps[] = {
	ZCAP_NET_ADMIN,
	ZCAP_NET_RAW,
    };
    zebra_privs_t privs = {};
    privs.user = user;
    privs.group = group;
#ifdef VTY_GROUP
    privs.vty_group = VTY_GROUP;
#endif
    privs.caps_p = caps;
    privs.cap_num_p = sizeof(caps) / sizeof(caps[0]);

    quagga_signal_t sigs[] = {
	{
	    SIGHUP,
	    sighup,
	},
	{
	    SIGINT,
	    sigint,
	},
	{
	    SIGTERM,
	    sigterm,
	},
	{
	    SIGUSR1,
	    sigusr1,
	},
    };

    EventLoop eventloop;

    //
    // ZebraRouter Node
    //
    ZebraRouterNode zebra_router_node(eventloop, daemonize, config_file,
				      SYSCONFDIR XPIMD_DEFAULT_CONFIG,
				      pid_file, zebra_socket,
				      vty_addr, vty_port,
				      vtysh_path, dryrun, privs, sigs,
				      sizeof(sigs) / sizeof(sigs[0]));
    zebra_router_node.init();

    //
    // MFEA node
    //
    ZebraMfeaNode zebra_mfea_node(family,
				  XORP_MODULE_MFEA,
				  eventloop,
				  zebra_router_node);
    zebra_mfea_node.init();

    //
    // MLD/IGMP node
    //
    ZebraMld6igmpNode zebra_mld6igmp_node(family,
					  XORP_MODULE_MLD6IGMP,
					  eventloop,
					  zebra_router_node,
					  zebra_mfea_node);
    zebra_mld6igmp_node.init();

    //
    // PIM node
    //
    ZebraPimNode zebra_pimsm_node(family,
				  XORP_MODULE_PIMSM,
				  eventloop,
				  zebra_router_node,
				  zebra_mfea_node,
				  zebra_mld6igmp_node);
    zebra_pimsm_node.init();

    if (!dryrun)
    {
	string error_msg;
	int r;

	r = zebra_mfea_node.start(error_msg);
	if (r != XORP_OK)
	{
	    XLOG_ERROR("starting mfea node failed: %s", error_msg.c_str());
	    return;
	}

	r = zebra_mld6igmp_node.start(error_msg);
	if (r != XORP_OK)
	{
	    XLOG_ERROR("starting mld6igmp node failed: %s", error_msg.c_str());
	    return;
	}

	r = zebra_pimsm_node.start(error_msg);
	if (r != XORP_OK)
	{
	    XLOG_ERROR("starting pimsm node failed: %s", error_msg.c_str());
	    return;
	}
    }

    // this should be done after the clients are initialized
    zebra_router_node.zebra_start();

    if (dryrun)
	return;

    //
    // Main loop
    //
    try {
	for (;;)
	{
	    quagga_sigevent_process();
	    if (terminated)
		break;
	    eventloop.run();
	}

	zebra_pimsm_node.terminate();
	zebra_mld6igmp_node.terminate();
	zebra_mfea_node.terminate();
	zebra_router_node.terminate();

	eventloop.run_pending_tasks();

	if (zlog_default)
	{
	    closezlog (zlog_default);
	    zlog_default = NULL;
	}
    } catch (XorpException &e) {
	XLOG_ERROR("xorp exception occurred: %s", e.str().c_str());
    } catch (exception &e) {
	XLOG_ERROR("standard exception occurred: %s", e.what());
    } catch (...) {
	XLOG_ERROR("unknown exception occurred");
    }
}

static int
zebra_log(void *obj, const char *msg)
{
    if (zlog_default != NULL && zlog_default->fp != NULL) {
	zlog_err("%s", msg);
    } else {
	fprintf(stderr, "%s\n", msg);
    }

    return 0;
}

int
main(int argc, char *argv[])
{
    bool daemonize = false;
    const char *config_file = NULL;
    const char *pid_file = PATH_XPIMD_PID;
    const char *zebra_socket = NULL;
    const char *vty_addr = NULL;
    uint16_t vty_port = XPIMD_VTY_PORT;
#ifdef QUAGGA_USER
    const char *user = QUAGGA_USER;
#else
    const char *user = NULL;
#endif
#ifdef QUAGGA_GROUP
    const char *group = QUAGGA_GROUP;
#else
    const char *group = NULL;
#endif
    const char *vtysh_path = XPIMD_VTYSH_PATH; // XXX add command-line option?
    bool dryrun = false;
    int family = AF_INET;

    umask(0027);		// setup file creation permissions

    //
    // Initialize and start xlog
    //
    xlog_init(argv[0], NULL);
    xlog_set_verbose(XLOG_VERBOSE_LOW);		// Least verbose messages
    // By default all logging levels are enabled
    // Increase the verbosity of warning and error messages
    xlog_level_set_verbose(XLOG_LEVEL_WARNING, XLOG_VERBOSE_HIGH);
    xlog_level_set_verbose(XLOG_LEVEL_ERROR, XLOG_VERBOSE_HIGH);
    xlog_add_output_func(zebra_log, NULL);
    xlog_start();

    //
    // Get the program options
    //
    const struct option opts[] = {
	{"daemon",      no_argument,       NULL, 'd'},
	{"config_file", required_argument, NULL, 'f'},
	{"pid_file",    required_argument, NULL, 'i'},
	{"socket",      required_argument, NULL, 'z'},
	{"vty_addr",    required_argument, NULL, 'A'},
	{"vty_port",    required_argument, NULL, 'P'},
	{"user",        required_argument, NULL, 'u'},
	{"group",       required_argument, NULL, 'g'},
	{"version",     no_argument,       NULL, 'v'},
	{"dryrun",      no_argument,       NULL, 'C'},
	{"help",        no_argument,       NULL, 'h'},
	{"ipv4",        no_argument,       NULL, '4'},
	{"ipv6",        no_argument,       NULL, '6'},
	{},
    };

    for (;;) {
	int ch = getopt_long(argc, argv, "df:i:z:A:P:u:g:vCh4"
#ifdef HAVE_IPV6_MULTICAST
			     "6"
#endif	// HAVE_IPV6_MULTICAST
			     , opts, NULL);
	if (ch == -1)
	    break;
	switch (ch) {
	case 'd':
	    daemonize = true;
	    break;

	case 'f':
	    config_file = optarg;
	    break;

	case 'i':
	    pid_file = optarg;
	    break;

	case 'z':
	    zebra_socket = optarg;
	    break;

	case 'A':
	    vty_addr = optarg;
	    break;

	case 'P':
	{
	    char *endptr;
	    unsigned long tmp = strtoul(optarg, &endptr, 0);
	    if (*optarg == '\0' || *endptr != '\0' || tmp > UINT16_MAX)
		usage(1, "invalid vty port: '%s'", optarg);
	    vty_port = tmp;
	    break;
	}

	case 'u':
	    user = optarg;
	    break;

	case 'g':
	    group = optarg;
	    break;

	case 'v':
	    print_version(__progname);
	    exit(0);
	    break;

	case 'C':
	    dryrun = true;
	    break;

	case 'h':
	case '?':
	    // Help
	    usage(0, NULL);
	    // NOTREACHED
	    break;

	case '4':
	    family = AF_INET;
	    break;

#ifdef HAVE_IPV6_MULTICAST
	case '6':
	    family = AF_INET6;
	    break;
#endif	// HAVE_IPV6_MULTICAST

	default:
	    usage(1, NULL);
	    // NOTREACHED
	    break;
	}
    }

    argc -= optind;
    argv += optind;
    if (argc != 0) {
	usage(1, NULL);
	// NOTREACHED
    }

    //
    // Run everything
    //
    try {
	multicast_main(daemonize, config_file, pid_file, zebra_socket,
		       vty_addr, vty_port, user, group,
		       vtysh_path, dryrun, family);
    } catch(...) {
	xorp_catch_standard_exceptions();
    }

    //
    // Gracefully stop and exit xlog
    //
    xlog_stop();
    xlog_exit();

    int status = terminated ? 1 : 0;
    exit(status);
}
