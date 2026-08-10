#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#include "ak_thread.h"
#include "ak_common.h"
#include "ak_vi.h"
#include "ak_venc.h"
#include "ak_rtsp.h"

#define DEFAULT_CONFIG_PATH          "/etc/jffs2/"
#define LEN_HINT                    512
#define DEFAULT_MAIN_WIDTH          1920
#define DEFAULT_MAIN_HEIGHT         1080
#define DEFAULT_SUB_WIDTH           640
#define DEFAULT_SUB_HEIGHT          360
#define DEFAULT_MAIN_FPS            25
#define DEFAULT_SUB_FPS             25
#define DEFAULT_MINQP               28
#define DEFAULT_MAXQP               42
#define DEFAULT_GOP                 2 
#define DEFAULT_MAIN_MODE           BR_MODE_CBR
#define DEFAULT_SUB_MODE            BR_MODE_CBR
#define DEFAULT_MAIN_TYPE           HEVC_ENC_TYPE
#define DEFAULT_SUB_TYPE            HEVC_ENC_TYPE
#define DEFAULT_MAIN_KBPS           2000
#define DEFAULT_SUB_KBPS            200
#define DEFAULT_MAIN_SUFFIX         "vs0"
#define DEFAULT_SUB_SUFFIX          "vs1"

char *config_path = DEFAULT_CONFIG_PATH;
int skip_sensor_match = 0;

char ac_option_hint[  ][ LEN_HINT ] = {
    "       HELP" ,
    "[NUM]  ( DEFAULT: 1920 )" ,
    "[NUM]  ( DEFAULT: 1080 )" ,
    "[NUM]  ( DEFAULT: 640 )" ,
    "[NUM]  ( DEFAULT: 360 )" ,
    "[NUM]  ( DEFAULT: 2000 )" ,
    "[NUM]  ( DEFAULT: 200 )" ,
    "[NAME] ( DEFAULT: 'vs0' )" ,
    "[NAME] ( DEFAULT: 'vs1' )" ,
    "[NUM]  ( DEFAULT: 50 )" ,
    "[NUM]  ( DEFAULT: 28 )" ,
    "[NUM]  ( DEFAULT: 42 )" ,
    "[PATH] ( DEFAULT: '/etc/jffs2/' )" ,
    "[FLAG] Skip sensor match (ignore error)" ,
};

struct option option_long[ ] = {
    { "help"             , no_argument       , NULL , 'h' } ,
    { "main-width"       , required_argument , NULL , 'a' } ,
    { "main-height "     , required_argument , NULL , 'b' } ,
    { "sub-width"        , required_argument , NULL , 'c' } ,
    { "sub-height"       , required_argument , NULL , 'd' } ,
    { "main-kbps"        , required_argument , NULL , 'e' } ,
    { "sub-kbps"         , required_argument , NULL , 'f' } ,
    { "main-chn-name"    , required_argument , NULL , 'n' } ,
    { "sub-chn-name"     , required_argument , NULL , 'o' } ,
    { "gop"              , required_argument , NULL , 'p' } ,
    { "minqp"            , required_argument , NULL , 'q' } ,
    { "maxqp"            , required_argument , NULL , 'r' } ,
    { "config-path"      , required_argument , NULL , 'P' } ,
    { "skip-match"       , no_argument       , NULL , 's' } ,
};

int i_main_width = DEFAULT_MAIN_WIDTH;
int i_main_height = DEFAULT_MAIN_HEIGHT;
int i_sub_width = DEFAULT_SUB_WIDTH;
int i_sub_height = DEFAULT_SUB_HEIGHT;
int i_main_kbps = DEFAULT_MAIN_KBPS;
int i_sub_kbps = DEFAULT_SUB_KBPS;
int i_main_fps = DEFAULT_MAIN_FPS;
int i_sub_fps = DEFAULT_SUB_FPS;
int i_main_mode = DEFAULT_MAIN_MODE;
int i_sub_mode = DEFAULT_SUB_MODE;
int i_main_type = DEFAULT_MAIN_TYPE;
int i_sub_type = DEFAULT_SUB_TYPE;
char *pc_main_name = DEFAULT_MAIN_SUFFIX;
char *pc_sub_name = DEFAULT_SUB_SUFFIX;
int i_gop = DEFAULT_GOP;
int i_minqp = DEFAULT_MINQP;
int i_maxqp = DEFAULT_MAXQP;
char *pc_prog_name = NULL ;

static int run_flag = AK_FALSE;

static void *ak_rtsp_vi_init(void)
{
    printf("[DEBUG] === Enter ak_rtsp_vi_init ===\n");
    printf("[DEBUG] config_path = %s\n", config_path);
    printf("[DEBUG] skip_sensor_match = %d\n", skip_sensor_match);

    struct stat st;
    if (stat(config_path, &st) != 0) {
        printf("[ERROR] Path %s does NOT exist or is inaccessible!\n", config_path);
        return NULL;
    }
    printf("[DEBUG] Path %s exists.\n", config_path);

    // 始终调用 ak_vi_match_sensor，但根据 skip 标志决定是否检查返回值
    printf("[DEBUG] Calling ak_vi_match_sensor(\"%s\") ...\n", config_path);
    int ret = ak_vi_match_sensor(config_path);
    if (ret < 0) {
        printf("[DEBUG] ak_vi_match_sensor returned %d\n", ret);
        if (!skip_sensor_match) {
            ak_print_error_ex("match sensor failed\n");
            printf("[ERROR] ak_vi_match_sensor returned error and skip flag is NOT set.\n");
            return NULL;
        } else {
            printf("[DEBUG] ak_vi_match_sensor failed but skip flag is set, continuing anyway.\n");
        }
    } else {
        printf("[DEBUG] ak_vi_match_sensor succeeded.\n");
    }

    /* 打开设备 */
    void *handle = ak_vi_open(VIDEO_DEV0);
    if (handle == NULL) {
        ak_print_error_ex("vi open failed\n");
        printf("[ERROR] ak_vi_open failed.\n");
        return NULL;
    }
    printf("[DEBUG] ak_vi_open succeeded, handle = %p\n", handle);

    /* 获取传感器分辨率 */
    struct video_resolution resolution = {0};
    if (ak_vi_get_sensor_resolution(handle, &resolution)) {
        ak_print_error_ex("get sensor resolution failed\n");
        ak_vi_close(handle);
        return NULL;
    }
    printf("[DEBUG] Sensor resolution: width=%d, height=%d\n", resolution.width, resolution.height);

    /* 设置通道属性 */
    struct video_channel_attr attr;
    attr.crop.left = 0;
    attr.crop.top = 0;
    attr.crop.width = resolution.width;
    attr.crop.height = resolution.height;

    attr.res[VIDEO_CHN_MAIN].width = i_main_width;
    attr.res[VIDEO_CHN_MAIN].height = i_main_height;
    attr.res[VIDEO_CHN_MAIN].max_width = 1920;
    attr.res[VIDEO_CHN_MAIN].max_height = 1080;

    attr.res[VIDEO_CHN_SUB].width = i_sub_width;
    attr.res[VIDEO_CHN_SUB].height= i_sub_height;
    attr.res[VIDEO_CHN_SUB].max_width = 640;
    attr.res[VIDEO_CHN_SUB].max_height = 480;

    if (ak_vi_set_channel_attr(handle, &attr)) {
        ak_print_error_ex("set channel attribute failed\n");
        ak_vi_close(handle);
        return NULL;
    }
    printf("[DEBUG] Channel attributes set successfully.\n");

    ak_print_notice_ex("start capture ...\n");
    if(ak_vi_capture_on(handle)) {
        ak_print_error_ex("start capture failed\n");
        ak_vi_close(handle);
        return NULL;
    }
    printf("[DEBUG] Capture started successfully.\n");

    return handle;
}

static int help_hint(void)
{
    int i ;
    printf( "%s\n" , pc_prog_name ) ;
    for( i = 0 ; i < sizeof( option_long ) / sizeof( struct option ) ; i ++ ) {
        printf( "\t--%-16s -%c %s\n" , option_long[ i ].name , option_long[ i ].val , ac_option_hint[ i ] ) ;
    }
    printf( "\n\n" ) ;
    return 0 ;
}

static void process_signal(unsigned int sig, siginfo_t *si, void *ptr)
{
    system("rm -f /tmp/core_*");
    ak_backtrace(sig, si, ptr);
    run_flag = AK_FALSE;
}

static int register_signal(void)
{
    struct sigaction s;
    s.sa_flags = SA_SIGINFO;
    s.sa_sigaction = (void *)process_signal;
    sigaction(SIGSEGV, &s, NULL);
    sigaction(SIGINT, &s, NULL);
    sigaction(SIGTERM, &s, NULL);
    sigaction(SIGUSR1, &s, NULL);
    sigaction(SIGUSR2, &s, NULL);
    sigaction(SIGALRM, &s, NULL);
    sigaction(SIGHUP, &s, NULL);
    sigaction(SIGPIPE, &s, NULL);
    signal(SIGCHLD, SIG_IGN);
    return 0;
}

int main(int argc, char **argv)
{
    int ret , i_option ;
    void *vi_handle;

    pc_prog_name = argv[ 0 ] ;

    register_signal();

    while( ( i_option = getopt_long( argc , argv , "ha:b:c:d:e:f:g:i:j:k:l:m:n:o:p:q:r:P:s" , option_long , NULL ) ) != -1 ) {
        switch( i_option ) {
            case 'h' :
                help_hint( ) ;
                return 0 ;
            case 'a' :
                i_main_width = atoi( optarg ) ;
                break;
            case 'b' :
                i_main_height = atoi( optarg ) ;
                break;
            case 'c' :
                i_sub_width = atoi( optarg ) ;
                break;
            case 'd' :
                i_sub_height = atoi( optarg ) ;
                break;
            case 'e' :
                i_main_kbps = atoi( optarg ) ;
                break;
            case 'f' :
                i_sub_kbps = atoi( optarg ) ;
                break;
            case 'g' :
                i_main_fps = atoi( optarg ) ;
                break;
            case 'i' :
                i_sub_fps = atoi( optarg ) ;
                break;
            case 'j' :
                i_main_mode = atoi( optarg ) ;
                break;
            case 'k' :
                i_sub_mode = atoi( optarg ) ;
                break;
            case 'l' :
                i_main_type = atoi( optarg ) ;
                break;
            case 'm' :
                i_sub_type = atoi( optarg ) ;
                break;
            case 'n' :
                pc_main_name = optarg ;
                break;
            case 'o' :
                pc_sub_name = optarg ;
                break;
            case 'p' :
                i_gop = atoi( optarg ) ;
                break;
            case 'q' :
                i_minqp = atoi( optarg ) ;
                break;
            case 'r' :
                i_maxqp = atoi( optarg ) ;
                break;
            case 'P' :
                config_path = optarg ;
                printf("[INFO] Config path set to: %s\n", config_path);
                break;
            case 's' :
                skip_sensor_match = 1 ;
                printf("[INFO] Will skip sensor match error (ignore failure).\n");
                break;
            default:
                help_hint();
                return 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);

    /* init camera*/
    vi_handle = ak_rtsp_vi_init();
    if(vi_handle == NULL) {
        ak_print_error("vi init fail\n");
        return -1;
    }
    ak_print_notice("vi init ok\n");

    struct rtsp_param param = {{{0}}};

    /* main channel config */
    param.rtsp_chn[0].current_channel = 0;
    param.rtsp_chn[0].width     = i_main_width;
    param.rtsp_chn[0].height    = i_main_height;
    param.rtsp_chn[0].fps       = i_main_fps;
    param.rtsp_chn[0].max_kbps  = i_main_kbps;
    param.rtsp_chn[0].min_qp    = i_minqp;
    param.rtsp_chn[0].max_qp    = i_maxqp;
    param.rtsp_chn[0].gop_len   = i_gop;
    param.rtsp_chn[0].video_enc_type = i_main_type;
    param.rtsp_chn[0].video_br_mode  = i_main_mode;
    param.rtsp_chn[0].vi_handle = vi_handle;
    strcpy(param.rtsp_chn[0].suffix_name, "vs0");

    /* sub channel config */
    param.rtsp_chn[1].current_channel = 1;
    param.rtsp_chn[1].width     = i_sub_width;
    param.rtsp_chn[1].height    = i_sub_height;
    param.rtsp_chn[1].fps       = i_sub_fps;
    param.rtsp_chn[1].max_kbps  = i_sub_kbps;
    param.rtsp_chn[1].min_qp    = i_minqp;
    param.rtsp_chn[1].max_qp    = i_maxqp;
    param.rtsp_chn[1].gop_len   = i_gop;
    param.rtsp_chn[1].video_enc_type = i_sub_type;
    param.rtsp_chn[1].video_br_mode  = i_sub_mode;
    param.rtsp_chn[1].vi_handle = vi_handle;
    strcpy(param.rtsp_chn[1].suffix_name, "vs1");

    /* init rtsp */
    ret = ak_rtsp_init(&param);
    ak_print_notice("init rtsp, ret: %d\n", ret);
    if (ret) {
        ak_print_error_ex("\n\t---- init rtsp failed ---- !\n");
        return -1;
    }
    /* start rtsp service */
    ak_rtsp_start(VIDEO_CHN_MAIN);
    ak_rtsp_start(VIDEO_CHN_SUB);

    run_flag = AK_TRUE;

    while (run_flag) {
        ak_sleep_ms(1000);
    }
    ak_rtsp_stop(VIDEO_CHN_MAIN);
    ak_rtsp_stop(VIDEO_CHN_SUB);

    ak_rtsp_exit();

    return 0;
}
