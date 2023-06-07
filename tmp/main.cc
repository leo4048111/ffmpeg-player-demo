
#include <Windows.h> //By Tiger Lee
#include <eh.h> //By Tiger Lee
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.ah"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#include <imagehlp.h>
#include "MyPlayer.h"

#pragma warning(disable:4018 4305 4244 4819 4996 4101 4267)
#pragma comment(lib, "dbghelp.lib")

#ifdef _WIN32
#undef main /* We don't want SDL to override our main() */
#endif

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 250
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

static unsigned sws_flags = SWS_FAST_BILINEAR;
static fnLogCallBack pLogCallback = NULL;
static SDL_mutex *audio_Mutex = NULL;

typedef enum ShowMode {
	SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
}ShowMode;

typedef struct MyAVPacketList {
	AVPacket pkt;
	struct MyAVPacketList *next;
	int serial;
} MyAVPacketList;

typedef struct PacketQueue {
	MyAVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	int64_t duration;
	int abort_request;
	int serial;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} AudioParams;

typedef struct Clock {
	double pts;           /* clock base */
	double pts_drift;     /* clock base minus time at which we updated the clock */
	double last_updated;
	double speed;
	int serial;           /* clock is based on a packet with this serial */
	int paused;
	int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
	AVFrame *frame;
	AVSubtitle sub;
	int serial;
	double pts;           /* presentation timestamp for the frame */
	double duration;      /* estimated duration of the frame */
	int64_t pos;          /* byte position of the frame in the input file */
	int width;
	int height;
	int format;
	AVRational sar;
	int uploaded;
	int flip_v;
} Frame;

typedef struct FrameQueue {
	Frame queue[FRAME_QUEUE_SIZE];
	int rindex;
	int windex;
	int size;
	int max_size;
	int keep_last;
	int rindex_shown;
	SDL_mutex *mutex;
	SDL_cond *cond;
	PacketQueue *pktq;
} FrameQueue;

enum {
	AV_SYNC_AUDIO_MASTER, /* default choice */
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
	AVPacket pkt;
	PacketQueue *queue;
	AVCodecContext *avctx;
	int pkt_serial;
	int finished;
	int packet_pending;
	SDL_cond *empty_queue_cond;
	int64_t start_pts;
	AVRational start_pts_tb;
	int64_t next_pts;
	AVRational next_pts_tb;
	SDL_Thread *decoder_tid;
} Decoder;

/* options specified by the user */
typedef struct VideoParam
{
	 AVInputFormat *file_iformat;
	 char *input_filename;
	 char *window_title;
	 int default_width;
	 int default_height;
	 int screen_width;
	 int screen_height;
	 int screen_left;
	 int screen_top;
	 int audio_disable;
	 int video_disable;
	 int subtitle_disable;
	 char* wanted_stream_spec[AVMEDIA_TYPE_NB];
	 int seek_by_bytes;
	 float seek_interval;
	 int display_disable;
	 int borderless;
	 int startup_volume;
	 int show_status;
	 int av_sync_type;
	 int64_t start_time;
	 int64_t duration;
	 int fast;
	 int genpts;
	 int lowres;
	 int decoder_reorder_pts;
	 int autoexit;
	 int exit_on_keydown;
	 int exit_on_mousedown;
	 int loop;
	 int framedrop;
	 int infinite_buffer;
	 ShowMode show_mode;
	 char *audio_codec_name;
	 char *subtitle_codec_name;
	 char *video_codec_name;
	 double rdftspeed;
	 int64_t cursor_last_shown;
	 int cursor_hidden;
	 int autorotate;
	 int find_stream_info;
	 int filter_nbthreads;

	/* current context */
	 int is_full_screen;
	 int64_t audio_callback_time;

	#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

	 HWND hWndPlayer;
	 SDL_Window *window;
	 SDL_Renderer *renderer;
	 SDL_RendererInfo renderer_info;
	 SDL_AudioDeviceID audio_dev;
	 char *audio_device_name;

	 AVDictionary *codec_opts;
	 AVDictionary *format_opts;

	 fnVideoFormatCallBack vidFormatCallback;
	 fnVideoCallBack       vidFrameCallback;
	 fnAudioMeterCallBack  audMeterCallback;
	 fnAudioCallBack       audFrameCallback;

	 fnPlayPosCallBack     playPosCallback;
	 fnPlayStateCallBack   playStateCallback;

	 int customAudio_Channels;
	 int customAudio_Samplerate;
	 int keepVideoRatio;
}VideoParam;


typedef struct VideoState {
	SDL_Thread *read_tid;
	AVInputFormat *iformat;
	int abort_request;
	int force_refresh;
	int paused;
	int last_paused;
	int queue_attachments_req;
	int seek_req;
	int seek_flags;
	int64_t seek_pos;
	int64_t seek_rel;
	int read_pause_return;
	AVFormatContext *ic;
	int realtime;

	Clock audclk;
	Clock vidclk;
	Clock extclk;

	FrameQueue pictq;
	FrameQueue subpq;
	FrameQueue sampq;

	Decoder auddec;
	Decoder viddec;
	Decoder subdec;

	int audio_stream;

	int av_sync_type;

	double audio_clock;
	int audio_clock_serial;
	double audio_diff_cum; /* used for AV difference average computation */
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	AVStream *audio_st;
	PacketQueue audioq;
	int audio_hw_buf_size;
	uint8_t *audio_buf;
	uint8_t *audio_buf1;
	unsigned int audio_buf_size; /* in bytes */
	unsigned int audio_buf1_size;
	int audio_buf_index; /* in bytes */
	int audio_write_buf_size;
	int audio_volume;
	int muted;
	struct AudioParams audio_src;
	struct AudioParams audio_tgt;
	struct SwrContext *swr_ctx;
	int frame_drops_early;
	int frame_drops_late;

	ShowMode show_mode;

	int16_t sample_array[SAMPLE_ARRAY_SIZE];
	int sample_array_index;
	int last_i_start;
	RDFTContext *rdft;
	int rdft_bits;
	FFTSample *rdft_data;
	int xpos;
	double last_vis_time;
	SDL_Texture *vis_texture;
	SDL_Texture *sub_texture;
	SDL_Texture *vid_texture;

	int subtitle_stream;
	AVStream *subtitle_st;
	PacketQueue subtitleq;

	double frame_timer;
	double frame_last_returned_time;
	double frame_last_filter_delay;
	int video_stream;
	AVStream *video_st;
	PacketQueue videoq;
	double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
	struct SwsContext *img_convert_ctx;
	struct SwsContext *sub_convert_ctx;
	int eof;

	char *filename;
	int width, height, xleft, ytop;
	int step;

	int last_video_stream, last_audio_stream, last_subtitle_stream;

	SDL_cond   *continue_read_thread;
	SDL_Thread *event_loop_id;

	VideoParam *avp;

	float ppmLeft;
	float ppmRight;
	int   ppmCount;
	int64_t last_pos_send_time;
	int64_t last_read_frame_time;
	int is_network_error;
} VideoState;

static AVPacket flush_pkt;
static const struct TextureFormatEntry {
	enum AVPixelFormat format;
	int texture_fmt;
} sdl_texture_format_map[] = {
	{ AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
	{ AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
	{ AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
	{ AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
	{ AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
	{ AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
	{ AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
	{ AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
	{ AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
	{ AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
	{ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
	{ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
	{ AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
	{ AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
	{ AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
	{ AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
	{ AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
	{ AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
	{ AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
	{ AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
};

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
	enum AVSampleFormat fmt2, int64_t channel_count2)
{
	/* If channel count == 1, planar and non-planar formats are the same */
	if (channel_count1 == 1 && channel_count2 == 1)
		return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
	else
		return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
	if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
		return channel_layout;
	else
		return 0;
}

static int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
	int ret = avformat_match_stream_specifier(s, st, spec);
	if (ret < 0)
		av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
	return ret;
}

static inline int GenerateMiniDump(EXCEPTION_POINTERS* pExceptionPointers)
{
	BOOL bMiniDumpSuccessful;
	TCHAR szFileName[MAX_PATH];
	HANDLE hDumpFile;
	SYSTEMTIME stLocalTime;
	MINIDUMP_EXCEPTION_INFORMATION ExpParam;

	GetLocalTime(&stLocalTime);

	sprintf(szFileName, ".\\%s-%04d%02d%02d-%02d%02d%02d.dmp",
		"XPlayer",
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);
	hDumpFile = CreateFile(szFileName, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	ExpParam.ThreadId = GetCurrentThreadId();
	ExpParam.ExceptionPointers = pExceptionPointers;
	ExpParam.ClientPointers = TRUE;

	bMiniDumpSuccessful = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
		hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
	
	CloseHandle(hDumpFile);

	return EXCEPTION_EXECUTE_HANDLER;
}

static long __stdcall myExceptionProcessor(_EXCEPTION_POINTERS* excp)
{
	av_log(NULL, AV_LOG_FATAL, "XPlayer runtime got unhandled exception...Exit");
	return GenerateMiniDump(excp);
}

static AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
	AVFormatContext *s, AVStream *st, AVCodec *codec)
{
	AVDictionary    *ret = NULL;
	AVDictionaryEntry *t = NULL;
	int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
		: AV_OPT_FLAG_DECODING_PARAM;
	char          prefix = 0;
	const AVClass    *cc = avcodec_get_class();

	if (!codec)
		codec = s->oformat ? avcodec_find_encoder(codec_id)
		: avcodec_find_decoder(codec_id);

	switch (st->codecpar->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		prefix = 'v';
		flags |= AV_OPT_FLAG_VIDEO_PARAM;
		break;
	case AVMEDIA_TYPE_AUDIO:
		prefix = 'a';
		flags |= AV_OPT_FLAG_AUDIO_PARAM;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		prefix = 's';
		flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
		break;
	}

	while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
		char *p = strchr(t->key, ':');

		/* check stream specification in opt name */
		if (p)
			switch (check_stream_specifier(s, st, p + 1)) {
			case  1: *p = 0; break;
			case  0:         continue;
			}

		if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
			!codec ||
			(codec->priv_class &&
				av_opt_find(&codec->priv_class, t->key, NULL, flags,
					AV_OPT_SEARCH_FAKE_OBJ)))
			av_dict_set(&ret, t->key, t->value, 0);
		else if (t->key[0] == prefix &&
			av_opt_find(&cc, t->key + 1, NULL, flags,
				AV_OPT_SEARCH_FAKE_OBJ))
			av_dict_set(&ret, t->key + 1, t->value, 0);

		if (p)
			*p = ':';
	}
	return ret;
}

static AVDictionary **setup_find_stream_info_opts(AVFormatContext *s,
	AVDictionary *codec_opts)
{
	int i;
	AVDictionary **opts;

	if (!s->nb_streams)
		return NULL;
	opts = (AVDictionary **)av_mallocz_array(s->nb_streams, sizeof(*opts));
	if (!opts) {
		av_log(NULL, AV_LOG_ERROR,
			"Could not alloc memory for stream options.\n");
		return NULL;
	}
	for (i = 0; i < s->nb_streams; i++)
		opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id,
			s, s->streams[i], NULL);
	return opts;
}

static void init_video_parameters(VideoParam * param)
{
	param->default_width = 640;
	param->default_height = 480;
	param->screen_left = SDL_WINDOWPOS_CENTERED;
	param->screen_top = SDL_WINDOWPOS_CENTERED;
	param->seek_by_bytes = -1;
	param->seek_interval = 0.04;
	param->startup_volume = 100;
	param->av_sync_type = AV_SYNC_AUDIO_MASTER;
	param->start_time = AV_NOPTS_VALUE;
	param->duration = AV_NOPTS_VALUE;
	param->decoder_reorder_pts = -1;
	param->loop = 0;
	param->framedrop = -1;
	param->infinite_buffer = -1;
	param->show_mode = SHOW_MODE_NONE;
	param->rdftspeed = 0.02;
	param->autorotate=1;
	param->find_stream_info = 1;

	param->customAudio_Channels=2;
	param->customAudio_Samplerate=48000;
	param->keepVideoRatio = 1;
}

static void reset_video_parameters(VideoParam * param)
{
	param->default_width = 640;
	param->default_height = 480;
	param->screen_left = SDL_WINDOWPOS_CENTERED;
	param->screen_top = SDL_WINDOWPOS_CENTERED;
	param->seek_by_bytes = -1;
	param->seek_interval = 0.04;
	param->av_sync_type = AV_SYNC_AUDIO_MASTER;
	param->start_time = AV_NOPTS_VALUE;
	param->duration = AV_NOPTS_VALUE;
	param->decoder_reorder_pts = -1;
	param->loop = 0;
	param->framedrop = -1;
	param->infinite_buffer = -1;
	param->show_mode = SHOW_MODE_NONE;
	param->rdftspeed = 0.02;
	param->autorotate = 1;
	param->find_stream_info = 1;
}


static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
	MyAVPacketList *pkt1;

	if (q->abort_request)
		return -1;

	pkt1 = (MyAVPacketList*)av_malloc(sizeof(MyAVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	if (pkt == &flush_pkt)
		q->serial++;
	pkt1->serial = q->serial;

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size + sizeof(*pkt1);
	q->duration += pkt1->pkt.duration;
	/* XXX: should duplicate packet data in DV case */
	SDL_CondSignal(q->cond);
	return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	int ret;

	SDL_LockMutex(q->mutex);
	ret = packet_queue_put_private(q, pkt);
	SDL_UnlockMutex(q->mutex);

	if (pkt != &flush_pkt && ret < 0)
		av_packet_unref(pkt);

	return ret;
}

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
	AVPacket pkt1, *pkt = &pkt1;
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;
	pkt->stream_index = stream_index;
	return packet_queue_put(q, pkt);
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	if (!q->mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->cond = SDL_CreateCond();
	if (!q->cond) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->abort_request = 1;
	return 0;
}

static void packet_queue_flush(PacketQueue *q)
{
	MyAVPacketList *pkt, *pkt1;

	SDL_LockMutex(q->mutex);
	for (pkt = q->first_pkt; pkt; pkt = pkt1) {
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->duration = 0;
	SDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
	packet_queue_flush(q);
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
	SDL_LockMutex(q->mutex);

	q->abort_request = 1;

	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
	SDL_LockMutex(q->mutex);
	q->abort_request = 0;
	packet_queue_put_private(q, &flush_pkt);
	SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
	MyAVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {
		if (q->abort_request) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size + sizeof(*pkt1);
			q->duration -= pkt1->pkt.duration;
			*pkt = pkt1->pkt;
			if (serial)
				*serial = pkt1->serial;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

static void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
	memset(d, 0, sizeof(Decoder));
	d->avctx = avctx;
	d->queue = queue;
	d->empty_queue_cond = empty_queue_cond;
	d->start_pts = AV_NOPTS_VALUE;
	d->pkt_serial = -1;
}
static int decoder_decode_frame(VideoParam *vp, Decoder *d, AVFrame *frame, AVSubtitle *sub) {
	int ret = AVERROR(EAGAIN);

	for (;;) {
		AVPacket pkt;

		if (d->queue->serial == d->pkt_serial) {
			do {
				if (d->queue->abort_request)
					return -1;

				switch (d->avctx->codec_type) {
				case AVMEDIA_TYPE_VIDEO:
					ret = avcodec_receive_frame(d->avctx, frame);
					if (ret >= 0) {
						if (vp->decoder_reorder_pts == -1) {
							frame->pts = frame->best_effort_timestamp;
						}
						else if (!vp->decoder_reorder_pts) {
							frame->pts = frame->pkt_dts;
						}
					}
					break;
				case AVMEDIA_TYPE_AUDIO:
					ret = avcodec_receive_frame(d->avctx, frame);
					if (ret >= 0) {
						//AVRational tb = (AVRational) { 1, frame->sample_rate };
						AVRational tb = { 1, frame->sample_rate };
						if (frame->pts != AV_NOPTS_VALUE)
							frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
						else if (d->next_pts != AV_NOPTS_VALUE)
							frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
						if (frame->pts != AV_NOPTS_VALUE) {
							d->next_pts = frame->pts + frame->nb_samples;
							d->next_pts_tb = tb;
						}
					}
					break;
				}
				if (ret == AVERROR_EOF) {
					d->finished = d->pkt_serial;
					avcodec_flush_buffers(d->avctx);
					return 0;
				}
				if (ret >= 0)
					return 1;
			} while (ret != AVERROR(EAGAIN));
		}

		do {
			if (d->queue->nb_packets == 0)
				SDL_CondSignal(d->empty_queue_cond);
			if (d->packet_pending) {
				av_packet_move_ref(&pkt, &d->pkt);
				d->packet_pending = 0;
			}
			else {
				if (packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial) < 0)
					return -1;
			}
		} while (d->queue->serial != d->pkt_serial);

		if (pkt.data == flush_pkt.data) {
			avcodec_flush_buffers(d->avctx);
			d->finished = 0;
			d->next_pts = d->start_pts;
			d->next_pts_tb = d->start_pts_tb;
		}
		else {
			if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
				int got_frame = 0;
				ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &pkt);
				if (ret < 0) {
					ret = AVERROR(EAGAIN);
				}
				else {
					if (got_frame && !pkt.data) {
						d->packet_pending = 1;
						av_packet_move_ref(&d->pkt, &pkt);
					}
					ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
				}
			}
			else {
				if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
					av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
					d->packet_pending = 1;
					av_packet_move_ref(&d->pkt, &pkt);
				}
			}
			av_packet_unref(&pkt);
		}
	}
}

static void decoder_destroy(Decoder *d) {
	av_packet_unref(&d->pkt);
	avcodec_free_context(&d->avctx);
}

static void frame_queue_unref_item(Frame *vp)
{
	av_frame_unref(vp->frame);
	avsubtitle_free(&vp->sub);
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
	int i;
	memset(f, 0, sizeof(FrameQueue));
	if (!(f->mutex = SDL_CreateMutex())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	if (!(f->cond = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	f->pktq = pktq;
	f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
	f->keep_last = !!keep_last;
	for (i = 0; i < f->max_size; i++)
		if (!(f->queue[i].frame = av_frame_alloc()))
			return AVERROR(ENOMEM);
	return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
	int i;
	for (i = 0; i < f->max_size; i++) {
		Frame *vp = &f->queue[i];
		frame_queue_unref_item(vp);
		av_frame_free(&vp->frame);
	}
	SDL_DestroyMutex(f->mutex);
	SDL_DestroyCond(f->cond);
}

static void frame_queue_signal(FrameQueue *f)
{
	SDL_LockMutex(f->mutex);
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

static Frame *frame_queue_peek(FrameQueue *f)
{
	return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
	return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

static Frame *frame_queue_peek_last(FrameQueue *f)
{
	return &f->queue[f->rindex];
}

static Frame *frame_queue_peek_writable(FrameQueue *f)
{
	/* wait until we have space to put a new frame */
	SDL_LockMutex(f->mutex);
	while (f->size >= f->max_size &&
		!f->pktq->abort_request) {
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	if (f->pktq->abort_request)
		return NULL;

	return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
	/* wait until we have a readable a new frame */
	SDL_LockMutex(f->mutex);
	while (f->size - f->rindex_shown <= 0 &&
		!f->pktq->abort_request) {
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	if (f->pktq->abort_request)
		return NULL;

	return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static void frame_queue_push(FrameQueue *f)
{
	if (++f->windex == f->max_size)
		f->windex = 0;
	SDL_LockMutex(f->mutex);
	f->size++;
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

static void frame_queue_next(FrameQueue *f)
{
	if (f->keep_last && !f->rindex_shown) {
		f->rindex_shown = 1;
		return;
	}
	frame_queue_unref_item(&f->queue[f->rindex]);
	if (++f->rindex == f->max_size)
		f->rindex = 0;
	SDL_LockMutex(f->mutex);
	f->size--;
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
	return f->size - f->rindex_shown;
}

/* return last shown position */
static int64_t frame_queue_last_pos(FrameQueue *f)
{
	Frame *fp = &f->queue[f->rindex];
	if (f->rindex_shown && fp->serial == f->pktq->serial)
		return fp->pos;
	else
		return -1;
}

static void decoder_abort(Decoder *d, FrameQueue *fq)
{
	packet_queue_abort(d->queue);
	frame_queue_signal(fq);
	SDL_WaitThread(d->decoder_tid, NULL);
	d->decoder_tid = NULL;
	packet_queue_flush(d->queue);
}

static inline void fill_rectangle(VideoParam *vp, int x, int y, int w, int h)
{
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	if (w && h)
		SDL_RenderFillRect(vp->renderer, &rect);
}

static int realloc_texture(VideoParam *vp, SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
	Uint32 format;
	int access, w, h;
	if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
		void *pixels;
		int pitch;
		if (*texture)
			SDL_DestroyTexture(*texture);
		if (!(*texture = SDL_CreateTexture(vp->renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
			return -1;
		if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
			return -1;
		if (init_texture) {
			if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
				return -1;
			memset(pixels, 0, pitch * new_height);
			SDL_UnlockTexture(*texture);
		}
		av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
	}
	return 0;
}

static void calculate_display_rect(SDL_Rect *rect,
	int scr_xleft, int scr_ytop, int scr_width, int scr_height,
	int pic_width, int pic_height, AVRational pic_sar)
{
	AVRational aspect_ratio = pic_sar;
	int64_t width, height, x, y;

	if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
		aspect_ratio = av_make_q(1, 1);

	aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

	/* XXX: we suppose the screen has a 1.0 pixel ratio */
	height = scr_height;
	width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
	if (width > scr_width) {
		width = scr_width;
		height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
	}
	x = (scr_width - width) / 2;
	y = (scr_height - height) / 2;
	rect->x = scr_xleft + x;
	rect->y = scr_ytop + y;
	rect->w = FFMAX((int)width, 1);
	rect->h = FFMAX((int)height, 1);
}

static void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode)
{
	int i;
	*sdl_blendmode = SDL_BLENDMODE_NONE;
	*sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
	if (format == AV_PIX_FMT_RGB32 ||
		format == AV_PIX_FMT_RGB32_1 ||
		format == AV_PIX_FMT_BGR32 ||
		format == AV_PIX_FMT_BGR32_1)
		*sdl_blendmode = SDL_BLENDMODE_BLEND;
	for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
		if (format == sdl_texture_format_map[i].format) {
			*sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
			return;
		}
	}
}

static int upload_texture(VideoParam *vp, SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
	int ret = 0;
	Uint32 sdl_pix_fmt;
	SDL_BlendMode sdl_blendmode;
	get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
	if (realloc_texture(vp, tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
		return -1;
	switch (sdl_pix_fmt) {
	case SDL_PIXELFORMAT_UNKNOWN:
		/* This should only happen if we are not using avfilter... */
		*img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
			frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
			AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
		if (*img_convert_ctx != NULL) {
			uint8_t *pixels[4];
			int pitch[4];
			if (!SDL_LockTexture(*tex, NULL, (void **)pixels, pitch)) {
				sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
					0, frame->height, pixels, pitch);
				SDL_UnlockTexture(*tex);
			}
		}
		else {
			av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
			ret = -1;
		}
		break;
	case SDL_PIXELFORMAT_IYUV:
		if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
			ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
				frame->data[1], frame->linesize[1],
				frame->data[2], frame->linesize[2]);
		}
		else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
			ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
				frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
				frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
		}
		else {
			av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
			return -1;
		}
		break;
	default:
		if (frame->linesize[0] < 0) {
			ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
		}
		else {
			ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
		}
		break;
	}
	return ret;
}

static void set_sdl_yuv_conversion_mode(AVFrame *frame)
{
#if SDL_VERSION_ATLEAST(2,0,8)
	SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;
	if (frame && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUYV422 || frame->format == AV_PIX_FMT_UYVY422)) {
		if (frame->color_range == AVCOL_RANGE_JPEG)
			mode = SDL_YUV_CONVERSION_JPEG;
		else if (frame->colorspace == AVCOL_SPC_BT709)
			mode = SDL_YUV_CONVERSION_BT709;
		else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M || frame->colorspace == AVCOL_SPC_SMPTE240M)
			mode = SDL_YUV_CONVERSION_BT601;
	}
	SDL_SetYUVConversionMode(mode);
#endif
}

static void video_image_display(VideoState *is)
{
	Frame *vp;
	Frame *sp = NULL;
	SDL_Rect rect;

	vp = frame_queue_peek_last(&is->pictq);
	if (is->subtitle_st) {
		if (frame_queue_nb_remaining(&is->subpq) > 0) {
			sp = frame_queue_peek(&is->subpq);

			if (vp->pts >= sp->pts + ((float)sp->sub.start_display_time / 1000)) {
				if (!sp->uploaded) {
					uint8_t* pixels[4];
					int pitch[4];
					int i;
					if (!sp->width || !sp->height) {
						sp->width = vp->width;
						sp->height = vp->height;
					}
					if (realloc_texture(is->avp, &is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
						return;

					for (i = 0; i < sp->sub.num_rects; i++) {
						AVSubtitleRect *sub_rect = sp->sub.rects[i];

						sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
						sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
						sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
						sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

						is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
							0, NULL, NULL, NULL);
						if (!is->sub_convert_ctx) {
							av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
							return;
						}
						if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch)) {
							sws_scale(is->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
								0, sub_rect->h, pixels, pitch);
							SDL_UnlockTexture(is->sub_texture);
						}
					}
					sp->uploaded = 1;
				}
			}
			else
				sp = NULL;
		}
	}

	if (is->avp->hWndPlayer == NULL)
	{
		if (is->avp->keepVideoRatio)
		{
			calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);
		}
		else
		{
			rect.x = rect.y = 0;
			rect.w = is->width, rect.h = is->height;
		}
	}
	else
	{
		RECT winRect;
		GetWindowRect(is->avp->hWndPlayer, &winRect);
		if (is->avp->keepVideoRatio)
		{
			calculate_display_rect(&rect, 0, 0, winRect.right - winRect.left, winRect.bottom - winRect.top, vp->width, vp->height, vp->sar);
		}
		else
		{
			rect.x = rect.y = 0;
			rect.w = winRect.right - winRect.left, rect.h = winRect.bottom - winRect.top;
		}
	}

	if (!vp->uploaded) {
		if (upload_texture(is->avp,&is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
			return;
		vp->uploaded = 1;
		vp->flip_v = vp->frame->linesize[0] < 0;
	}
	
	//SDL_Rect t;
	//calculate_display_rect(&t, 0, 0, vp->width, vp->height, vp->width, vp->height, vp->sar);
	//av_log(NULL, AV_LOG_ERROR, "****%d,%d,%dx%d\n", t.x, t.y, t.w, t.h);

	set_sdl_yuv_conversion_mode(vp->frame);
	SDL_RenderCopyEx(is->avp->renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
	set_sdl_yuv_conversion_mode(NULL);
	if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
		SDL_RenderCopy(is->avp->renderer, is->sub_texture, NULL, &rect);
#else
		int i;
		double xratio = (double)rect.w / (double)sp->width;
		double yratio = (double)rect.h / (double)sp->height;
		for (i = 0; i < sp->sub.num_rects; i++) {
			SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
			SDL_Rect target = { .x = rect.x + sub_rect->x * xratio,
				.y = rect.y + sub_rect->y * yratio,
				.w = sub_rect->w * xratio,
				.h = sub_rect->h * yratio };
			SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);
		}
#endif
	}
}

static inline int compute_mod(int a, int b)
{
	return a < 0 ? a%b + b : a%b;
}

static void video_audio_display(VideoState *s)
{
	int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
	int ch, channels, h, h2;
	int64_t time_diff;
	int rdft_bits, nb_freq;

	for (rdft_bits = 1; (1 << rdft_bits) < 2 * s->height; rdft_bits++)
		;
	nb_freq = 1 << (rdft_bits - 1);

	/* compute display index : center on currently output samples */
	channels = s->audio_tgt.channels;
	nb_display_channels = channels;
	if (!s->paused) {
		int data_used = s->show_mode == SHOW_MODE_WAVES ? s->width : (2 * nb_freq);
		n = 2 * channels;
		delay = s->audio_write_buf_size;
		delay /= n;

		/* to be more precise, we take into account the time spent since
		the last buffer computation */
		if (s->avp->audio_callback_time) {
			time_diff = av_gettime_relative() - s->avp->audio_callback_time;
			delay -= (time_diff * s->audio_tgt.freq) / 1000000;
		}

		delay += 2 * data_used;
		if (delay < data_used)
			delay = data_used;

		i_start = x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
		if (s->show_mode == SHOW_MODE_WAVES) {
			h = INT_MIN;
			for (i = 0; i < 1000; i += channels) {
				int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
				int a = s->sample_array[idx];
				int b = s->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
				int c = s->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
				int d = s->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
				int score = a - d;
				if (h < score && (b ^ c) < 0) {
					h = score;
					i_start = idx;
				}
			}
		}

		s->last_i_start = i_start;
	}
	else {
		i_start = s->last_i_start;
	}

	if (s->show_mode == SHOW_MODE_WAVES) {
		SDL_SetRenderDrawColor(s->avp->renderer, 255, 255, 255, 255);

		/* total height for one channel */
		h = s->height / nb_display_channels;
		/* graph height / 2 */
		h2 = (h * 9) / 20;
		for (ch = 0; ch < nb_display_channels; ch++) {
			i = i_start + ch;
			y1 = s->ytop + ch * h + (h / 2); /* position of center line */
			for (x = 0; x < s->width; x++) {
				y = (s->sample_array[i] * h2) >> 15;
				if (y < 0) {
					y = -y;
					ys = y1 - y;
				}
				else {
					ys = y1;
				}
				fill_rectangle(s->avp,s->xleft + x, ys, 1, y);
				i += channels;
				if (i >= SAMPLE_ARRAY_SIZE)
					i -= SAMPLE_ARRAY_SIZE;
			}
		}

		SDL_SetRenderDrawColor(s->avp->renderer, 0, 0, 255, 255);

		for (ch = 1; ch < nb_display_channels; ch++) {
			y = s->ytop + ch * h;
			fill_rectangle(s->avp, s->xleft, y, s->width, 1);
		}
	}
	else {
		if (realloc_texture(s->avp, &s->vis_texture, SDL_PIXELFORMAT_ARGB8888, s->width, s->height, SDL_BLENDMODE_NONE, 1) < 0)
			return;

		nb_display_channels = FFMIN(nb_display_channels, 2);
		if (rdft_bits != s->rdft_bits) {
			av_rdft_end(s->rdft);
			av_free(s->rdft_data);
			s->rdft = av_rdft_init(rdft_bits, DFT_R2C);
			s->rdft_bits = rdft_bits;
			s->rdft_data = (FFTSample *)av_malloc_array(nb_freq, 4 * sizeof(*s->rdft_data));
		}
		if (!s->rdft || !s->rdft_data) {
			av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
			s->show_mode = SHOW_MODE_WAVES;
		}
		else {
			FFTSample *data[2];
			//SDL_Rect rect = { .x = s->xpos,.y = 0,.w = 1,.h = s->height };
			SDL_Rect rect = {s->xpos,0,1,s->height };
			uint32_t *pixels;
			int pitch;
			for (ch = 0; ch < nb_display_channels; ch++) {
				data[ch] = s->rdft_data + 2 * nb_freq * ch;
				i = i_start + ch;
				for (x = 0; x < 2 * nb_freq; x++) {
					double w = (x - nb_freq) * (1.0 / nb_freq);
					data[ch][x] = s->sample_array[i] * (1.0 - w * w);
					i += channels;
					if (i >= SAMPLE_ARRAY_SIZE)
						i -= SAMPLE_ARRAY_SIZE;
				}
				av_rdft_calc(s->rdft, data[ch]);
			}
			/* Least efficient way to do this, we should of course
			* directly access it but it is more than fast enough. */
			if (!SDL_LockTexture(s->vis_texture, &rect, (void **)&pixels, &pitch)) {
				pitch >>= 2;
				pixels += pitch * s->height;
				for (y = 0; y < s->height; y++) {
					double w = 1 / sqrt(nb_freq);
					int a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1]));
					int b = (nb_display_channels == 2) ? sqrt(w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))
						: a;
					a = FFMIN(a, 255);
					b = FFMIN(b, 255);
					pixels -= pitch;
					*pixels = (a << 16) + (b << 8) + ((a + b) >> 1);
				}
				SDL_UnlockTexture(s->vis_texture);
			}
			SDL_RenderCopy(s->avp->renderer, s->vis_texture, NULL, NULL);
		}
		if (!s->paused)
			s->xpos++;
		if (s->xpos >= s->width)
			s->xpos = s->xleft;
	}
}

static void stream_component_close(VideoState *is, int stream_index)
{
	AVFormatContext *ic = is->ic;
	AVCodecParameters *codecpar;

	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return;
	codecpar = ic->streams[stream_index]->codecpar;

	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		decoder_abort(&is->auddec, &is->sampq);
		SDL_CloseAudioDevice(is->avp->audio_dev);
		decoder_destroy(&is->auddec);
		swr_free(&is->swr_ctx);
		av_freep(&is->audio_buf1);
		is->audio_buf1_size = 0;
		is->audio_buf = NULL;

		if (is->rdft) {
			av_rdft_end(is->rdft);
			av_freep(&is->rdft_data);
			is->rdft = NULL;
			is->rdft_bits = 0;
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		decoder_abort(&is->viddec, &is->pictq);
		decoder_destroy(&is->viddec);
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		decoder_abort(&is->subdec, &is->subpq);
		decoder_destroy(&is->subdec);
		break;
	default:
		break;
	}

	ic->streams[stream_index]->discard = AVDISCARD_ALL;
	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio_st = NULL;
		is->audio_stream = -1;
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video_st = NULL;
		is->video_stream = -1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle_st = NULL;
		is->subtitle_stream = -1;
		break;
	default:
		break;
	}
}

static void stream_close(VideoState *is)
{
	/* XXX: use a special url_shutdown call to abort parse cleanly */
	is->abort_request = 1;
	SDL_WaitThread(is->read_tid, NULL);

	/* close each stream */
	if (is->audio_stream >= 0)
		stream_component_close(is, is->audio_stream);
	if (is->video_stream >= 0)
		stream_component_close(is, is->video_stream);
	if (is->subtitle_stream >= 0)
		stream_component_close(is, is->subtitle_stream);

	avformat_close_input(&is->ic);

	packet_queue_destroy(&is->videoq);
	packet_queue_destroy(&is->audioq);
	packet_queue_destroy(&is->subtitleq);

	/* free all pictures */
	frame_queue_destory(&is->pictq);
	frame_queue_destory(&is->sampq);
	frame_queue_destory(&is->subpq);
	SDL_DestroyCond(is->continue_read_thread);
	sws_freeContext(is->img_convert_ctx);
	sws_freeContext(is->sub_convert_ctx);
	
	av_free(is->filename);
	av_free(is->avp->input_filename);

	if (is->vis_texture)
		SDL_DestroyTexture(is->vis_texture);
	if (is->vid_texture)
		SDL_DestroyTexture(is->vid_texture);
	if (is->sub_texture)
		SDL_DestroyTexture(is->sub_texture);

	av_dict_free(&is->avp->format_opts);
	av_dict_free(&is->avp->codec_opts);
}

static void set_default_window_size(VideoParam *vp, int width, int height, AVRational sar)
{
	if (vp->hWndPlayer != NULL) return;

	SDL_Rect rect;
	int max_width = vp->screen_width ? vp->screen_width : INT_MAX;
	int max_height = vp->screen_height ? vp->screen_height : INT_MAX;
	if (max_width == INT_MAX || max_height == INT_MAX)
	{
		max_height = height;
		max_width = width;
	}
	calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
	vp->default_width = rect.w;
	vp->default_height = rect.h;
}

static int video_open(VideoState *is)
{
	int w, h;

	w = is->avp->screen_width ? is->avp->screen_width : is->avp->default_width;
	h = is->avp->screen_height ? is->avp->screen_height : is->avp->default_height;

	if (!is->avp->window_title)
		is->avp->window_title = is->avp->input_filename;

	if (is->avp->vidFrameCallback == NULL)
	{
		SDL_SetWindowTitle(is->avp->window, is->avp->window_title);

		SDL_SetWindowSize(is->avp->window, w, h);
		SDL_SetWindowPosition(is->avp->window, is->avp->screen_left, is->avp->screen_top);
		if (is->avp->is_full_screen)
			SDL_SetWindowFullscreen(is->avp->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_ShowWindow(is->avp->window);
	}

	is->width = w;
	is->height = h;

	return 0;
}

static AVFrame *scale_frame(AVFrame *f, int dst_width, int dst_height) {
	/*struct swsContext*/ 
	SwsContext *sws = sws_getContext(f->width, f->height, (AVPixelFormat)f->format,
		dst_width, dst_height,
		(AVPixelFormat)f->format, SWS_FAST_BILINEAR,
		NULL, NULL, NULL);
	if (!sws)
		return NULL;

	AVFrame *pCnvFrame = av_frame_alloc();
	pCnvFrame->format = f->format;
	pCnvFrame->width = dst_width;
	pCnvFrame->height = dst_height;
	av_frame_get_buffer(pCnvFrame, 32);
	av_frame_make_writable(pCnvFrame);

	int ret = sws_scale(sws, f->extended_data, f->linesize, 0, f->height,
		pCnvFrame->extended_data, pCnvFrame->linesize);

	sws_freeContext(sws);
	if (ret <= 0)
	{
		av_frame_free(&pCnvFrame);
		pCnvFrame = NULL;
	}
	return pCnvFrame;
}

/* display the current picture, if any */
static void video_display(VideoState *is)
{
	if (!is->width)
		video_open(is);

	if (is->avp->vidFrameCallback == NULL)
	{
		SDL_SetRenderDrawColor(is->avp->renderer, 0, 0, 0, 255);
		SDL_RenderClear(is->avp->renderer);
	}

	if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
	{
		if (is->avp->vidFrameCallback == NULL)
			video_audio_display(is);
	}
	else if (is->video_st)
	{
		if (is->avp->vidFrameCallback == NULL) {
			video_image_display(is);
		}
		else{
			Frame *vp = frame_queue_peek_last(&is->pictq);
			if (vp && vp->frame)
			{
				//计算显示大小
				SDL_Rect dispRect;
				calculate_display_rect(&dispRect, 0, 0, vp->frame->width, vp->frame->height, vp->frame->width, vp->frame->height, vp->sar);
				if (dispRect.w != vp->frame->width || dispRect.h != vp->frame->height)
				{
					AVFrame* f = scale_frame(vp->frame, dispRect.w, dispRect.h);
					is->avp->vidFrameCallback(is,
						f->width, f->height,
						f->data[0], f->linesize[0],
						f->data[1], f->linesize[1],
						f->data[2], f->linesize[2]);
					av_frame_free(&f);
				}
				else
				{
					is->avp->vidFrameCallback(is,
						vp->frame->width, vp->frame->height,
						vp->frame->data[0], vp->frame->linesize[0],
						vp->frame->data[1], vp->frame->linesize[1],
						vp->frame->data[2], vp->frame->linesize[2]);
				}
			}
		}
	}
	if (is->avp->vidFrameCallback == NULL)
		SDL_RenderPresent(is->avp->renderer);
}

static double get_clock(Clock *c)
{
	if (*c->queue_serial != c->serial)
		return NAN;
	if (c->paused) {
		return c->pts;
	}
	else {
		double time = av_gettime_relative() / 1000000.0;
		return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
	}
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
	c->pts = pts;
	c->last_updated = time;
	c->pts_drift = c->pts - time;
	c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
	double time = av_gettime_relative() / 1000000.0;
	set_clock_at(c, pts, serial, time);
}

static void set_clock_speed(Clock *c, double speed)
{
	set_clock(c, get_clock(c), c->serial);
	c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial)
{
	c->speed = 1.0;
	c->paused = 0;
	c->queue_serial = queue_serial;
	set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave)
{
	double clock = get_clock(c);
	double slave_clock = get_clock(slave);
	if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
		set_clock(c, slave_clock, slave->serial);
}

static int get_master_sync_type(VideoState *is) {
	if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		if (is->video_st)
			return AV_SYNC_VIDEO_MASTER;
		else
			return AV_SYNC_AUDIO_MASTER;
	}
	else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		if (is->audio_st)
			return AV_SYNC_AUDIO_MASTER;
		else
			return AV_SYNC_EXTERNAL_CLOCK;
	}
	else {
		return AV_SYNC_EXTERNAL_CLOCK;
	}
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
	double val;

	switch (get_master_sync_type(is)) {
	case AV_SYNC_VIDEO_MASTER:
		val = get_clock(&is->vidclk);
		break;
	case AV_SYNC_AUDIO_MASTER:
		val = get_clock(&is->audclk);
		break;
	default:
		val = get_clock(&is->extclk);
		break;
	}
	return val;
}

static void check_external_clock_speed(VideoState *is) {
	if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
		is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
		set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
	}
	else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
		(is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
		set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
	}
	else {
		double speed = is->extclk.speed;
		if (speed != 1.0)
			set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
	}
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
	if (!is->seek_req) {
		is->seek_pos = pos;
		is->seek_rel = rel;
		is->seek_flags &= ~AVSEEK_FLAG_BYTE;
		if (seek_by_bytes)
			is->seek_flags |= AVSEEK_FLAG_BYTE;
		is->seek_req = 1;
		SDL_CondSignal(is->continue_read_thread);
	}
}

/* pause or resume the video */
static void stream_toggle_pause(VideoState *is)
{
	if (is->paused) {
		is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
		if (is->read_pause_return != AVERROR(ENOSYS)) {
			is->vidclk.paused = 0;
		}
		set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
	}
	set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
	is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

static void toggle_pause(VideoState *is)
{
	stream_toggle_pause(is);
	is->step = 0;
}

static void toggle_mute(VideoState *is)
{
	is->muted = !is->muted;
}

static void update_volume(VideoState *is, int sign, double step)
{
	double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
	is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

static void step_to_next_frame(VideoState *is)
{
	/* if the stream is paused unpause it, then step */
	if (is->paused)
		stream_toggle_pause(is);
	is->step = 1;
}
static double compute_target_delay(double delay, VideoState *is)
{
	double sync_threshold, diff = 0;

	/* update delay to follow master synchronisation source */
	if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
		/* if video is slave, we try to correct big delays by
		duplicating or deleting a frame */
		diff = get_clock(&is->vidclk) - get_master_clock(is);
		/* skip or repeat frame. We take into account the
		delay to compute the threshold. I still don't know
		if it is the best guess */
		sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
		
		if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
			if (diff <= -sync_threshold)
				delay = FFMAX(0, delay + diff);
			else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
				delay = delay + diff;
			else if (diff >= sync_threshold)
				delay = 2 * delay;
		}
	}

	av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
		delay, -diff);

	return delay;
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
	if (vp->serial == nextvp->serial) {
		double duration = nextvp->pts - vp->pts;
		if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
			return vp->duration;
		else
			return duration;
	}
	else {
		return 0.0;
	}
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
	/* update current video pts */
	set_clock(&is->vidclk, pts, serial);
	sync_clock_to_slave(&is->extclk, &is->vidclk);
}

/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
	VideoState *is = (VideoState *)opaque;
	double time;

	Frame *sp, *sp2;

	if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
		check_external_clock_speed(is);

	if (!is->avp->display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
		time = av_gettime_relative() / 1000000.0;
		if (is->force_refresh || is->last_vis_time + is->avp->rdftspeed < time) {
			video_display(is);
			is->last_vis_time = time;
		}
		*remaining_time = FFMIN(*remaining_time, is->last_vis_time + is->avp->rdftspeed - time);
	}

	if (is->video_st) {
	retry:
		if (frame_queue_nb_remaining(&is->pictq) == 0) {
			// nothing to do, no picture to display in the queue
		}
		else {
			double last_duration, duration, delay;
			Frame *vp, *lastvp;

			/* dequeue the picture */
			lastvp = frame_queue_peek_last(&is->pictq);
			vp = frame_queue_peek(&is->pictq);

			if (vp->serial != is->videoq.serial) {
				frame_queue_next(&is->pictq);
				goto retry;
			}

			if (lastvp->serial != vp->serial)
				is->frame_timer = av_gettime_relative() / 1000000.0;

			if (is->paused)
				goto display;

			/* compute nominal last_duration */
			last_duration = vp_duration(is, lastvp, vp);
			delay = compute_target_delay(last_duration, is);
			
			time = av_gettime_relative() / 1000000.0;

			if (time < is->frame_timer + delay) {
				*remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
				goto display;
			}

			is->frame_timer += delay;
			if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
				is->frame_timer = time;
			
			SDL_LockMutex(is->pictq.mutex);
			if (!isnan(vp->pts)) {
				update_video_pts(is, vp->pts, vp->pos, vp->serial);
			}
			SDL_UnlockMutex(is->pictq.mutex);

			if (frame_queue_nb_remaining(&is->pictq) > 1) {
				Frame *nextvp = frame_queue_peek_next(&is->pictq);
				duration = vp_duration(is, vp, nextvp);
				if (!is->step && (is->avp->framedrop>0 || 
					(is->avp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER))
					&& time > is->frame_timer + duration) {
					is->frame_drops_late++;
					frame_queue_next(&is->pictq);
					goto retry;
				}
			}

			if (is->subtitle_st) {
				while (frame_queue_nb_remaining(&is->subpq) > 0) {
					sp = frame_queue_peek(&is->subpq);

					if (frame_queue_nb_remaining(&is->subpq) > 1)
						sp2 = frame_queue_peek_next(&is->subpq);
					else
						sp2 = NULL;

					if (sp->serial != is->subtitleq.serial
						|| (is->vidclk.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
						|| (sp2 && is->vidclk.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
					{
						if (sp->uploaded) {
							int i;
							for (i = 0; i < sp->sub.num_rects; i++) {
								AVSubtitleRect *sub_rect = sp->sub.rects[i];
								uint8_t *pixels;
								int pitch, j;

								if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
									for (j = 0; j < sub_rect->h; j++, pixels += pitch)
										memset(pixels, 0, sub_rect->w << 2);
									SDL_UnlockTexture(is->sub_texture);
								}
							}
						}
						frame_queue_next(&is->subpq);
					}
					else {
						break;
					}
				}
			}

			frame_queue_next(&is->pictq);
			is->force_refresh = 1;

			if (is->step && !is->paused)
				stream_toggle_pause(is);
		}
	display:
		/* display picture */
		if (!is->avp->display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown)
			video_display(is);
	}
	is->force_refresh = 0;

	if (is->avp->playPosCallback && is->ic) {
		int64_t cur_time;
		int aqsize, vqsize, sqsize;
		double av_diff;
		double pos = get_master_clock(is);

		if (isnan(pos))
		{
			pos = 0;
		}
		else
		{
			if (pos < 0) pos = 0;

			pos *= 1000;
			if (is->ic->duration >0 && pos > (is->ic->duration / 1000))
				pos = is->ic->duration / 1000;
		}

		cur_time = av_gettime_relative();
		if (!is->last_pos_send_time || (cur_time - is->last_pos_send_time) >= 30000) {
			aqsize = 0;
			vqsize = 0;
			sqsize = 0;
			if (is->audio_st)
				aqsize = is->audioq.nb_packets;
			if (is->video_st)
				vqsize = is->videoq.nb_packets;
			if (is->subtitle_st)
				sqsize = is->subtitleq.nb_packets;
			av_diff = 0;
			if (is->audio_st && is->video_st)
				av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
			else if (is->video_st)
				av_diff = get_master_clock(is) - get_clock(&is->vidclk);
			else if (is->audio_st)
				av_diff = get_master_clock(is) - get_clock(&is->audclk);
			//av_log(NULL, AV_LOG_INFO,
			//	"%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
			//	get_master_clock(is),
			//	(is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
			//	av_diff,
			//	is->frame_drops_early + is->frame_drops_late,
			//	aqsize / 1024,
			//	vqsize / 1024,
			//	sqsize,
			//	is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
			//	is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);
			if (aqsize == 0 && vqsize == 0 && is->eof && is->ic->duration > 0)
				pos = is->ic->duration / 1000;

			is->last_pos_send_time = cur_time;
			is->avp->playPosCallback(is, pos, is->ic->duration > 0 ? is->ic->duration / 1000 : 0, av_diff, vqsize, aqsize);
			//is->avp->playPosCallback(is, pos, is->ic->duration > 0 ? is->ic->duration / 1000 : 0, is->vidclk.pts*1000, is->audclk.pts*1000, av_diff*1000);
		}
	}
}

static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
	Frame *vp;

#if defined(DEBUG_SYNC)
	printf("frame_type=%c pts=%0.3f\n",
		av_get_picture_type_char(src_frame->pict_type), pts);
#endif

	if (!(vp = frame_queue_peek_writable(&is->pictq)))
		return -1;

	vp->sar = src_frame->sample_aspect_ratio;
	vp->uploaded = 0;

	vp->width = src_frame->width;
	vp->height = src_frame->height;
	vp->format = src_frame->format;

	vp->pts = pts;
	vp->duration = duration;
	vp->pos = pos;
	vp->serial = serial;

	set_default_window_size(is->avp, vp->width, vp->height, vp->sar);

	av_frame_move_ref(vp->frame, src_frame);
	frame_queue_push(&is->pictq);
	return 0;
}

static int get_video_frame(VideoState *is, AVFrame *frame)
{
	int got_picture;

	if ((got_picture = decoder_decode_frame(is->avp, &is->viddec, frame, NULL)) < 0)
		return -1;

	if (got_picture) {
		double dpts = NAN;

		if (frame->pts != AV_NOPTS_VALUE)
			dpts = av_q2d(is->video_st->time_base) * frame->pts;

		frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

		if (is->avp->framedrop>0 || (is->avp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
			if (frame->pts != AV_NOPTS_VALUE) {
				double diff = dpts - get_master_clock(is);
				if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
					diff - is->frame_last_filter_delay < 0 &&
					is->viddec.pkt_serial == is->vidclk.serial &&
					is->videoq.nb_packets) {
					is->frame_drops_early++;
					av_frame_unref(frame);
					got_picture = 0;
				}
			}
		}
	}
	return got_picture;
}

static int audio_thread(void *arg)
{
	VideoState *is = (VideoState *)arg;
	AVFrame *frame = av_frame_alloc();
	Frame *af;
	int got_frame = 0;
	AVRational tb;
	int ret = 0;

	if (!frame)
		return AVERROR(ENOMEM);
	
	do {
		if ((got_frame = decoder_decode_frame(is->avp, &is->auddec, frame, NULL)) < 0)
			goto the_end;
		
		if (got_frame) {
			tb.num = 1;
			tb.den = frame->sample_rate;

			if (!(af = frame_queue_peek_writable(&is->sampq)))
				goto the_end;

			af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
			af->pos = frame->pkt_pos;
			af->serial = is->auddec.pkt_serial;
			//af->duration = av_q2d((AVRational) { frame->nb_samples, frame->sample_rate });
			af->duration = av_q2d({frame->nb_samples, frame->sample_rate });

			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&is->sampq);
		}
	} while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

the_end:
	av_frame_free(&frame);
	return ret;
}

static int decoder_start(Decoder *d, int(*fn)(void *), const char *thread_name, void* arg)
{
	packet_queue_start(d->queue);
	d->decoder_tid = SDL_CreateThread(fn, thread_name, arg);
	if (!d->decoder_tid) {
		av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	return 0;
}

static int video_thread(void *arg)
{
	VideoState *is = (VideoState *)arg;
	AVFrame *frame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	AVRational tb = is->video_st->time_base;
	AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

	if (!frame)
		return AVERROR(ENOMEM);

	for (;;) {
		ret = get_video_frame(is, frame);
		if (ret < 0)
			goto the_end;
		if (!ret)
			continue;

			//duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational) { frame_rate.den, frame_rate.num }) : 0);
			duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
			pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
			ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
			av_frame_unref(frame);

		if (ret < 0)
			goto the_end;
	}

the_end:
	av_frame_free(&frame);
	return 0;
}

static int subtitle_thread(void *arg)
{
	VideoState *is = (VideoState *)arg;
	Frame *sp;
	int got_subtitle;
	double pts;

	for (;;) {
		if (!(sp = frame_queue_peek_writable(&is->subpq)))
			return 0;

		if ((got_subtitle = decoder_decode_frame(is->avp, &is->subdec, NULL, &sp->sub)) < 0)
			break;

		pts = 0;

		if (got_subtitle && sp->sub.format == 0) {
			if (sp->sub.pts != AV_NOPTS_VALUE)
				pts = sp->sub.pts / (double)AV_TIME_BASE;
			sp->pts = pts;
			sp->serial = is->subdec.pkt_serial;
			sp->width = is->subdec.avctx->width;
			sp->height = is->subdec.avctx->height;
			sp->uploaded = 0;

			/* now we can update the picture count */
			frame_queue_push(&is->subpq);
		}
		else if (got_subtitle) {
			avsubtitle_free(&sp->sub);
		}
	}
	return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
	int size, len;

	size = samples_size / sizeof(short);
	while (size > 0) {
		len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
		if (len > size)
			len = size;
		memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
		samples += len;
		is->sample_array_index += len;
		if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
			is->sample_array_index = 0;
		size -= len;
	}
}

/* return the wanted number of samples to get better sync if sync_type is video
* or external master clock */
static int synchronize_audio(VideoState *is, int nb_samples)
{
	int wanted_nb_samples = nb_samples;

	/* if not master, then we try to remove or add samples to correct the clock */
	if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
		double diff, avg_diff;
		int min_nb_samples, max_nb_samples;

		diff = get_clock(&is->audclk) - get_master_clock(is);

		if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
			is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
			if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
				/* not enough measures to have a correct estimate */
				is->audio_diff_avg_count++;
			}
			else {
				/* estimate the A-V difference */
				avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

				if (fabs(avg_diff) >= is->audio_diff_threshold) {
					wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
					min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
				}
				av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
					diff, avg_diff, wanted_nb_samples - nb_samples,
					is->audio_clock, is->audio_diff_threshold);
			}
		}
		else {
			/* too big difference : may be initial PTS errors, so
			reset A-V filter */
			is->audio_diff_avg_count = 0;
			is->audio_diff_cum = 0;
		}
	}

	return wanted_nb_samples;
}

/**
* Decode one audio frame and return its uncompressed size.
*
* The processed audio frame is decoded, converted if required, and
* stored in is->audio_buf, with size in bytes given by the return
* value.
*/
static int audio_decode_frame(VideoState *is)
{
	int data_size, resampled_data_size;
	int64_t dec_channel_layout;
	av_unused double audio_clock0;
	int wanted_nb_samples;
	Frame *af;

	if (is->paused)
		return -1;

	do {
		if (is->abort_request) return -1;

		while (frame_queue_nb_remaining(&is->sampq) == 0 && !is->abort_request) {
			if ((av_gettime_relative() - is->avp->audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
				return -1;
			av_usleep(1000);
		}

		if (!(af = frame_queue_peek_readable(&is->sampq)))
			return -1;
		frame_queue_next(&is->sampq);
	} while (af->serial != is->audioq.serial);

	data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
		af->frame->nb_samples,
		(AVSampleFormat)af->frame->format, 1);

	dec_channel_layout =
		(af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
		af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
	wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

	if (af->frame->format != is->audio_src.fmt ||
		dec_channel_layout != is->audio_src.channel_layout ||
		af->frame->sample_rate != is->audio_src.freq ||
		(wanted_nb_samples != af->frame->nb_samples && !is->swr_ctx)) {
		swr_free(&is->swr_ctx);
		is->swr_ctx = swr_alloc_set_opts(NULL,
			is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
			dec_channel_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
			0, NULL);
		if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
			av_log(NULL, AV_LOG_ERROR,
				"Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
				is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
			swr_free(&is->swr_ctx);
			return -1;
		}
		is->audio_src.channel_layout = dec_channel_layout;
		is->audio_src.channels = af->frame->channels;
		is->audio_src.freq = af->frame->sample_rate;
		is->audio_src.fmt = (AVSampleFormat)af->frame->format;
	}

	if (is->swr_ctx) {
		const uint8_t **in = (const uint8_t **)af->frame->extended_data;
		uint8_t **out = &is->audio_buf1;
		int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
		int out_size = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
		int len2;
		if (out_size < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		if (wanted_nb_samples != af->frame->nb_samples) {
			if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
				wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
				av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
				return -1;
			}
		}
		av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
		if (!is->audio_buf1)
			return AVERROR(ENOMEM);
		len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
		if (len2 < 0) {
			av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
			return -1;
		}
		if (len2 == out_count) {
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(is->swr_ctx) < 0)
				swr_free(&is->swr_ctx);
		}
		is->audio_buf = is->audio_buf1;
		resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
	}
	else {
		is->audio_buf = af->frame->data[0];
		resampled_data_size = data_size;
	}

	audio_clock0 = is->audio_clock;
	/* update the audio clock with the pts */
	
	if (!isnan(af->pts))
		is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
	else
		is->audio_clock = NAN;
	is->audio_clock_serial = af->serial;

	return resampled_data_size;
}

//计算跳表信息
static void calculateVolumeMeter(Uint8* pcmBuffer, int dataLen, VideoState *is)
{
	float leftMeter = 0.0f;
	float rightMeter = 0.0f;
	int len = dataLen - (dataLen % 4);

	for (int n = 0; n < len; n += 4)
	{
		float rawl = (((int8_t)pcmBuffer[n + 1] << 8) | pcmBuffer[n]) / 32768.0;
		float rawr = (((int8_t)pcmBuffer[n + 3] << 8) | pcmBuffer[n + 2]) / 32768.0;
		float leftMeter_t = rawl < 0 ? -rawl : rawl;
		float rightMeter_t = rawr < 0 ? -rawr : rawr;
		leftMeter = max(leftMeter, leftMeter_t);
		rightMeter = max(rightMeter, rightMeter_t);
	}
	is->ppmLeft = max(leftMeter, is->ppmLeft);
	is->ppmRight = max(rightMeter, is->ppmRight);
	is->ppmCount += dataLen / 4;
}

/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
	VideoState *is = (VideoState *)opaque;
	int audio_size, len1;

	if (is->abort_request || is->is_network_error) return;

	is->avp->audio_callback_time = av_gettime_relative();

	while (len > 0 && !is->abort_request) {
		if (is->audio_buf_index >= is->audio_buf_size) {
			audio_size = audio_decode_frame(is);
			if (audio_size < 0) {
				/* if error, just output silence */
				is->audio_buf = NULL;
				is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
			}
			else {
				if (is->show_mode != SHOW_MODE_VIDEO)
					update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
				is->audio_buf_size = audio_size;
			}
			is->audio_buf_index = 0;
		}
		len1 = is->audio_buf_size - is->audio_buf_index;
		if (len1 > len)
			len1 = len;

		if (is->avp->audMeterCallback != NULL && is->audio_buf)
		{
			calculateVolumeMeter((uint8_t *)is->audio_buf + is->audio_buf_index, len1, is);
			if (is->ppmCount >= (is->audio_tgt.freq / 10))
			{
				is->avp->audMeterCallback(is, is->ppmLeft, is->ppmRight);
				is->ppmCount = 0;
				is->ppmLeft = is->ppmRight = 0;
			}
		}

		memset(stream, 0, len1);
		if (is->avp->audFrameCallback != NULL)
		{
			is->avp->audFrameCallback(is, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
		}
		else
		{
			if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
			{
				memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);

			}
			else
			{
				if (!is->muted && is->audio_buf)
					SDL_MixAudioFormat(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1, is->audio_volume);
			}
		}
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}
	is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
	/* Let's assume the audio driver that is used by SDL has two periods. */
	if (!isnan(is->audio_clock)) {
		set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, is->avp->audio_callback_time / 1000000.0);
		sync_clock_to_slave(&is->extclk, &is->audclk);
	}
}

static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
	VideoState *is = (VideoState*)opaque;
	SDL_AudioSpec wanted_spec, spec;
	const char *env;
	static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
	static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
	int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

	env = SDL_getenv("SDL_AUDIO_CHANNELS");
	if (env) {
		wanted_nb_channels = atoi(env);
		wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
	}
	if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
		wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
		wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
	}
	wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
	wanted_spec.channels = wanted_nb_channels;
	wanted_spec.freq = wanted_sample_rate;
	if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
		av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
		return -1;
	}
	while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
		next_sample_rate_idx--;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.silence = 0;
	wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
	wanted_spec.callback = sdl_audio_callback;
	wanted_spec.userdata = opaque;
	//By Tiger Lee
	CoInitialize(NULL);

	while (!(is->avp->audio_dev = SDL_OpenAudioDevice(is->avp->audio_device_name, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
		av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
			wanted_spec.channels, wanted_spec.freq, SDL_GetError());
		wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
		if (!wanted_spec.channels) {
			wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
			wanted_spec.channels = wanted_nb_channels;
			if (!wanted_spec.freq) {
				av_log(NULL, AV_LOG_ERROR,
					"No more combinations to try, audio open failed\n");
				return -1;
			}
		}
		wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
	}
	if (spec.format != AUDIO_S16SYS) {
		av_log(NULL, AV_LOG_ERROR,
			"SDL advised audio format %d is not supported!\n", spec.format);
		return -1;
	}
	if (spec.channels != wanted_spec.channels) {
		wanted_channel_layout = av_get_default_channel_layout(spec.channels);
		if (!wanted_channel_layout) {
			av_log(NULL, AV_LOG_ERROR,
				"SDL advised channel count %d is not supported!\n", spec.channels);
			return -1;
		}
	}

	audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
	audio_hw_params->freq = spec.freq;
	audio_hw_params->channel_layout = wanted_channel_layout;
	audio_hw_params->channels = spec.channels;
	audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
	audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
	if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	return spec.size;
}

/* open a given stream. Return 0 if OK */
static int stream_component_open(VideoState *is, int stream_index)
{
	AVFormatContext *ic = is->ic;
	AVCodecContext *avctx;
	AVCodec *codec;
	const char *forced_codec_name = NULL;
	AVDictionary *opts = NULL;
	AVDictionaryEntry *t = NULL;
	int sample_rate, nb_channels;
	int64_t channel_layout;
	int ret = 0;
	int stream_lowres = is->avp->lowres;

	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return -1;

	avctx = avcodec_alloc_context3(NULL);
	if (!avctx)
		return AVERROR(ENOMEM);

	ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
	if (ret < 0)
		goto fail;
	avctx->pkt_timebase = ic->streams[stream_index]->time_base;

	codec = avcodec_find_decoder(avctx->codec_id);

	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO: is->last_audio_stream = stream_index; forced_codec_name = is->avp->audio_codec_name; break;
	case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = is->avp->subtitle_codec_name; break;
	case AVMEDIA_TYPE_VIDEO: is->last_video_stream = stream_index; forced_codec_name = is->avp->video_codec_name; break;
	}
	if (forced_codec_name)
		codec = avcodec_find_decoder_by_name(forced_codec_name);
	if (!codec) {
		if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
			"No codec could be found with name '%s'\n", forced_codec_name);
		else                   av_log(NULL, AV_LOG_WARNING,
			"No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
		ret = AVERROR(EINVAL);
		goto fail;
	}

	avctx->codec_id = codec->id;
	if (stream_lowres > codec->max_lowres) {
		av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
			codec->max_lowres);
		stream_lowres = codec->max_lowres;
	}
	avctx->lowres = stream_lowres;

	if (is->avp->fast)
		avctx->flags2 |= AV_CODEC_FLAG2_FAST;

	opts = filter_codec_opts(is->avp->codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
	if (!av_dict_get(opts, "threads", NULL, 0))
		av_dict_set(&opts, "threads", "auto", 0);
	if (stream_lowres)
		av_dict_set_int(&opts, "lowres", stream_lowres, 0);
	if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
		av_dict_set(&opts, "refcounted_frames", "1", 0);
	if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
		goto fail;
	}
	if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
		ret = AVERROR_OPTION_NOT_FOUND;
		goto fail;
	}

	is->eof = 0;
	ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:

		if (is->avp->customAudio_Channels == 0 ||
			is->avp->customAudio_Samplerate == 0)
		{
			sample_rate = avctx->sample_rate;
			nb_channels = avctx->channels;
			channel_layout = avctx->channel_layout;
		}
		else
		{
			sample_rate = is->avp->customAudio_Samplerate;
			nb_channels = is->avp->customAudio_Channels;
			channel_layout = av_get_default_channel_layout(nb_channels);
		}

		/* prepare audio output */
		SDL_LockMutex(audio_Mutex);
		if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
		{
			SDL_UnlockMutex(audio_Mutex);
			goto fail;
		}
		SDL_UnlockMutex(audio_Mutex);
		is->audio_hw_buf_size = ret;
		is->audio_src = is->audio_tgt;
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;

		/* init averaging filter */
		is->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
		is->audio_diff_avg_count = 0;
		/* since we do not have a precise anough audio FIFO fullness,
		we correct audio sync only if larger than this threshold */
		is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

		is->audio_stream = stream_index;
		is->audio_st = ic->streams[stream_index];

		decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
		if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
			is->auddec.start_pts = is->audio_st->start_time;
			is->auddec.start_pts_tb = is->audio_st->time_base;
		}
		if ((ret = decoder_start(&is->auddec, audio_thread, "audio_decoder", is)) < 0)
			goto out;
		SDL_PauseAudioDevice(is->avp->audio_dev, 0);
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video_stream = stream_index;
		is->video_st = ic->streams[stream_index];

		decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
		if ((ret = decoder_start(&is->viddec, video_thread, "video_decoder", is)) < 0)
			goto out;
		is->queue_attachments_req = 1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle_stream = stream_index;
		is->subtitle_st = ic->streams[stream_index];

		decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread);
		if ((ret = decoder_start(&is->subdec, subtitle_thread, "subtitle_decoder", is)) < 0)
			goto out;
		break;
	default:
		break;
	}
	goto out;

fail:
	avcodec_free_context(&avctx);
out:
	av_dict_free(&opts);

	return ret;
}

static int decode_interrupt_cb(void *ctx)
{
	VideoState *is = (VideoState *)ctx;
	int64_t gap = av_gettime_relative() - is->last_read_frame_time;
	is->is_network_error = (gap > 5000000);
	return (is->abort_request) || (gap > 5000000);
}

static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
	return stream_id < 0 ||
		queue->abort_request ||
		(st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

static int is_realtime(AVFormatContext *s)
{
	if (!strcmp(s->iformat->name, "rtp")
		|| !strcmp(s->iformat->name, "rtsp")
		|| !strcmp(s->iformat->name, "sdp")
		)
		return 1;

	if (s->pb && (!strncmp(s->url, "rtp:", 4)
		|| !strncmp(s->url, "udp:", 4)
		)
		)
		return 1;
	return 0;
}

// 将str字符以spl分割,存于dst中，并返回子字符串数量
static int split(char dst[][512], char* str, const char* spl)
{
	int n = 0;
	char *result = NULL;
	result = strtok(str, spl);
	while (result != NULL)
	{
		memset(dst[n], 0, sizeof(dst[0]));
		strcpy(dst[n++], result);
		result = strtok(NULL, spl);
	}
	return n;
}

/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
	VideoState *is = (VideoState *)arg;
	AVFormatContext *ic = NULL;
	int err, i, ret;
	int st_index[AVMEDIA_TYPE_NB];
	AVPacket pkt1, *pkt = &pkt1;
	int64_t stream_start_time;
	int pkt_in_play_range = 0;
	AVDictionaryEntry *t;
	SDL_mutex *wait_mutex = SDL_CreateMutex();
	int scan_all_pmts_set = 0;
	int64_t pkt_ts;
	int open_ok = 0;

	if (!wait_mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		ret = AVERROR(ENOMEM);
		goto fail;
	}
	
	memset(st_index, -1, sizeof(st_index));
	is->last_video_stream = is->video_stream = -1;
	is->last_audio_stream = is->audio_stream = -1;
	is->last_subtitle_stream = is->subtitle_stream = -1;
	is->eof = 0;
	is->last_read_frame_time = av_gettime_relative();
	is->is_network_error = 0;

	ic = avformat_alloc_context();
	if (!ic) {
		av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
		ret = AVERROR(ENOMEM);
		goto fail;
	}
	ic->interrupt_callback.callback = decode_interrupt_cb;
	ic->interrupt_callback.opaque = is;
	if (!av_dict_get(is->avp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
		av_dict_set(&is->avp->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
		scan_all_pmts_set = 1;
	}
	
	//+2022.05.23,在URL后面可以添加参数
	char params[32][512];
	int pcnt = split(params, is->filename, " ");
	for (int i = 1; i < pcnt; i++)
	{
		char kv[2][512];
		pcnt = split(kv, params[i], "=");
		av_dict_set(&is->avp->format_opts, kv[0], kv[1], 0);
	}
	err = avformat_open_input(&ic, params[0], is->iformat, &is->avp->format_opts);
	//err = avformat_open_input(&ic, is->filename, is->iformat, &is->avp->format_opts);

	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Open %s failed. error code=%d.\n", is->filename, err);
		ret = -1;
		goto fail;
	}
	if (scan_all_pmts_set)
		av_dict_set(&is->avp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

	if ((t = av_dict_get(is->avp->format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option '%s' not found.\n", t->key);
		ret = AVERROR_OPTION_NOT_FOUND;
		goto fail;
	}
	is->ic = ic;

	if (is->avp->genpts)
		ic->flags |= AVFMT_FLAG_GENPTS;

	av_format_inject_global_side_data(ic);

	if (is->avp->find_stream_info) {
		AVDictionary **opts = setup_find_stream_info_opts(ic, is->avp->codec_opts);
		int orig_nb_streams = ic->nb_streams;

		err = avformat_find_stream_info(ic, opts);

		for (i = 0; i < orig_nb_streams; i++)
			av_dict_free(&opts[i]);
		av_freep(&opts);

		if (err < 0) {
			av_log(NULL, AV_LOG_WARNING,
				"%s: could not find codec parameters\n", is->filename);
			ret = -1;
			goto fail;
		}
	}

	if (ic->pb)
		ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

	if (is->avp->seek_by_bytes < 0)
		is->avp->seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

	is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

	/* if seeking requested, we execute it */
	if (is->avp->start_time != AV_NOPTS_VALUE && is->avp->start_time > 0) {
		int64_t timestamp;

		timestamp = is->avp->start_time;
		/* add the stream start time */
		if (ic->start_time != AV_NOPTS_VALUE)
			timestamp += ic->start_time;
		ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
		if (ret < 0) {
			av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
				is->filename, (double)timestamp / AV_TIME_BASE);
		}
	}

	is->realtime = is_realtime(ic);

	if (is->avp->show_status)
		av_dump_format(ic, 0, is->filename, 0);

	for (i = 0; i < ic->nb_streams; i++) {
		AVStream *st = ic->streams[i];
		enum AVMediaType type = st->codecpar->codec_type;
		st->discard = AVDISCARD_ALL;
		if (type >= 0 && is->avp->wanted_stream_spec[type] && st_index[type] == -1)
			if (avformat_match_stream_specifier(ic, st, is->avp->wanted_stream_spec[type]) > 0)
				st_index[type] = i;
	}
	for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
		if (is->avp->wanted_stream_spec[i] && st_index[i] == -1) {
			av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", is->avp->wanted_stream_spec[i], av_get_media_type_string((AVMediaType)i));
			st_index[i] = INT_MAX;
		}
	}

	if (!is->avp->video_disable)
		st_index[AVMEDIA_TYPE_VIDEO] =
		av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
			st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
	if (!is->avp->audio_disable)
		st_index[AVMEDIA_TYPE_AUDIO] =
		av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
			st_index[AVMEDIA_TYPE_AUDIO],
			st_index[AVMEDIA_TYPE_VIDEO],
			NULL, 0);
	if (!is->avp->video_disable && !is->avp->subtitle_disable)
		st_index[AVMEDIA_TYPE_SUBTITLE] =
		av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
			st_index[AVMEDIA_TYPE_SUBTITLE],
			(st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
				st_index[AVMEDIA_TYPE_AUDIO] :
				st_index[AVMEDIA_TYPE_VIDEO]),
			NULL, 0);

	is->show_mode = is->avp->show_mode;
	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
		AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
		AVCodecParameters *codecpar = st->codecpar;
		AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
		//AVRational fr = av_guess_frame_rate(ic, st, NULL);
		if (codecpar->width)
			set_default_window_size(is->avp,codecpar->width, codecpar->height, sar);
	}

	/* open the streams */
	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
		stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
	}

	ret = -1;
	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
		ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
	}
	if (is->show_mode == SHOW_MODE_NONE)
		is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

	if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
		stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
	}

	if (is->video_stream < 0 && is->audio_stream < 0) {
		av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
			is->filename);
		ret = -1;
		goto fail;
	}

	if (is->avp->infinite_buffer < 0 && is->realtime)
		is->avp->infinite_buffer = 1;

#pragma region "Callback notification"

	if (is->avp->playStateCallback != NULL)
		is->avp->playStateCallback(is, PlayState_Opened);

	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0 && is->avp->vidFormatCallback != NULL)
	{
		AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
		SDL_Rect dispRect;
		calculate_display_rect(&dispRect, 0, 0, st->codec->width, st->codec->height, st->codec->width, st->codec->height, st->codec->sample_aspect_ratio);
		is->avp->vidFormatCallback(is, dispRect.w, dispRect.h, st->codec->pix_fmt);

		//is->avp->vidFormatCallback(is, st->codec->width, st->codec->height, st->codec->pix_fmt);
	}

	if (is->avp->playStateCallback != NULL)
		is->avp->playStateCallback(is, PlayState_Playing);

	open_ok = 1;

#pragma endregion
	
	for (;;) {
		if (is->abort_request)
			break;
		if (is->paused != is->last_paused) {
			is->last_paused = is->paused;
			if (is->paused)
				is->read_pause_return = av_read_pause(ic);
			else
				av_read_play(ic);

			if (is->avp->playStateCallback != NULL)
				is->avp->playStateCallback(is, is->paused ? PlayState_Paused : PlayState_Playing);
		}
		if (is->paused &&
			(!strcmp(ic->iformat->name, "rtsp") ||
			(ic->pb && !strncmp(is->avp->input_filename, "mmsh:", 5)))) {
			/* wait 10 ms to avoid trying to get another packet */
			/* XXX: horrible */
			SDL_Delay(10);
			continue;
		}
		if (is->seek_req) {
			int64_t seek_target = is->seek_pos;
			int64_t seek_min = is->seek_rel > 0 ? seek_target - is->seek_rel + 2 : INT64_MIN;
			int64_t seek_max = is->seek_rel < 0 ? seek_target - is->seek_rel - 2 : INT64_MAX;

			ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR,
					"%s: error while seeking\n", is->ic->url);
			}
			else {
				if (is->audio_stream >= 0) {
					packet_queue_flush(&is->audioq);
					packet_queue_put(&is->audioq, &flush_pkt);
				}
				if (is->subtitle_stream >= 0) {
					packet_queue_flush(&is->subtitleq);
					packet_queue_put(&is->subtitleq, &flush_pkt);
				}
				if (is->video_stream >= 0) {
					packet_queue_flush(&is->videoq);
					packet_queue_put(&is->videoq, &flush_pkt);
				}
				if (is->seek_flags & AVSEEK_FLAG_BYTE) {
					set_clock(&is->extclk, NAN, 0);
				}
				else {
					set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
				}
			}
			is->seek_req = 0;
			is->queue_attachments_req = 1;
			is->eof = 0;
			if (is->paused)
				step_to_next_frame(is);
		}
		if (is->queue_attachments_req) {
			if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				AVPacket copy = { 0 };
				if ((ret = av_packet_ref(&copy, &is->video_st->attached_pic)) < 0)
					goto fail;
				packet_queue_put(&is->videoq, &copy);
				packet_queue_put_nullpacket(&is->videoq, is->video_stream);
			}
			is->queue_attachments_req = 0;
		}

		/* if the queue are full, no need to read more */
		if (is->avp->infinite_buffer<1 &&
			(is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
				|| (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
					stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
					stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
			/* wait 10 ms */
			SDL_LockMutex(wait_mutex);
			SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
			SDL_UnlockMutex(wait_mutex);
			continue;
		}
		if (!is->paused &&
			(!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
			(!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {
			if (is->avp->loop/* != 1 && (!is->avp->loop || --is->avp->loop)*/) {
				stream_seek(is, is->avp->start_time != AV_NOPTS_VALUE ? is->avp->start_time : 0, 0, 0);
			}
			else if (is->avp->autoexit) {
				ret = AVERROR_EOF;
				goto fail;
			}
		}
		is->last_read_frame_time = av_gettime_relative();
		is->is_network_error = 0;
		ret = av_read_frame(ic, pkt);
		//-541478725 = AVERROR_EOF
		//av_log(NULL, AV_LOG_ERROR, "Read--->ret=%d   autoexit=%d  eof=%d error=%d\n", ret, is->avp->autoexit, is->eof, ic->pb->error);
		if (ret < 0) {
			if (ret == AVERROR_EXIT) break;

			if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
				if (is->video_stream >= 0)
					packet_queue_put_nullpacket(&is->videoq, is->video_stream);
				if (is->audio_stream >= 0)
					packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
				if (is->subtitle_stream >= 0)
					packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
				is->eof = 1;
			}
			if (ic->pb && ic->pb->error)
				break;

			if (is->eof && is->avp->autoexit)
			{
				break;
			}

			SDL_LockMutex(wait_mutex);
			SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
			SDL_UnlockMutex(wait_mutex);
			continue;
		}
		else {
			is->eof = 0;
		}

		/* check if packet is in play range specified by user, then queue, otherwise discard */
		stream_start_time = ic->streams[pkt->stream_index]->start_time;
		pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
		pkt_in_play_range = is->avp->duration == AV_NOPTS_VALUE ||
			(pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
			av_q2d(ic->streams[pkt->stream_index]->time_base) -
			(double)(is->avp->start_time != AV_NOPTS_VALUE ? is->avp->start_time : 0) / 1000000
			<= ((double)is->avp->duration / 1000000);

		if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
			packet_queue_put(&is->audioq, pkt);
		}
		else if (pkt->stream_index == is->video_stream && pkt_in_play_range
			&& !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			packet_queue_put(&is->videoq, pkt);
		}
		else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
			packet_queue_put(&is->subtitleq, pkt);
		}
		else {
			av_packet_unref(pkt);
		}
	}

	ret = 0;
fail:

	if (ic && !is->ic)
		avformat_close_input(&ic);

	if (ret != 0) {
		SDL_Event event;

		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	SDL_DestroyMutex(wait_mutex);

	if (open_ok == 0)
	{
		if(is->avp->playStateCallback)
			is->avp->playStateCallback(is, PlayState_Error);
	}
	else
	{
		if (is->avp->playStateCallback)
		{
			if (ret < 0 && ret != AVERROR_EOF)
				is->avp->playStateCallback(is, PlayState_NetError);
			else if (is->eof)
			{
				while (is->abort_request != 1 && (is->audioq.size + is->videoq.size + is->subtitleq.size) > 0)
					av_usleep(10000);
				is->avp->playStateCallback(is, is->is_network_error ? PlayState_NetError : PlayState_End);
			}
		}
	}
	return 0;
}

static int stream_open(VideoState *is, const char *filename, AVInputFormat *iformat)
{
	if (!is || !is->avp)
		return -2;

	is->filename = av_strdup(filename);
	if (!is->filename)
		goto fail;
	is->iformat = iformat;
	is->ytop = 0;
	is->xleft = 0;
	
	/* start video display */
	if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
		goto fail;
	if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
		goto fail;
	if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto fail;

	if (packet_queue_init(&is->videoq) < 0 ||
		packet_queue_init(&is->audioq) < 0 ||
		packet_queue_init(&is->subtitleq) < 0)
		goto fail;

	if (!(is->continue_read_thread = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		goto fail;
	}

	init_clock(&is->vidclk, &is->videoq.serial);
	init_clock(&is->audclk, &is->audioq.serial);
	init_clock(&is->extclk, &is->extclk.serial);
	is->audio_clock_serial = -1;
	is->avp->startup_volume = av_clip(is->avp->startup_volume, 0, 100);
	is->avp->startup_volume = av_clip(SDL_MIX_MAXVOLUME * is->avp->startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
	is->audio_volume = is->avp->startup_volume;
	is->muted = 0;
	is->av_sync_type = is->avp->av_sync_type;
	is->read_tid = SDL_CreateThread(read_thread, "read_thread", is);
	if (!is->read_tid) 
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
	fail:
		stream_close(is);
		return -1;
	}
	return 0;
}

static void stream_cycle_channel(VideoState *is, int codec_type)
{
	AVFormatContext *ic = is->ic;
	int start_index, stream_index;
	int old_index;
	AVStream *st;
	AVProgram *p = NULL;
	int nb_streams = is->ic->nb_streams;

	if (codec_type == AVMEDIA_TYPE_VIDEO) {
		start_index = is->last_video_stream;
		old_index = is->video_stream;
	}
	else if (codec_type == AVMEDIA_TYPE_AUDIO) {
		start_index = is->last_audio_stream;
		old_index = is->audio_stream;
	}
	else {
		start_index = is->last_subtitle_stream;
		old_index = is->subtitle_stream;
	}
	stream_index = start_index;

	if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
		p = av_find_program_from_stream(ic, NULL, is->video_stream);
		if (p) {
			nb_streams = p->nb_stream_indexes;
			for (start_index = 0; start_index < nb_streams; start_index++)
				if (p->stream_index[start_index] == stream_index)
					break;
			if (start_index == nb_streams)
				start_index = -1;
			stream_index = start_index;
		}
	}

	for (;;) {
		if (++stream_index >= nb_streams)
		{
			if (codec_type == AVMEDIA_TYPE_SUBTITLE)
			{
				stream_index = -1;
				is->last_subtitle_stream = -1;
				goto the_end;
			}
			if (start_index == -1)
				return;
			stream_index = 0;
		}
		if (stream_index == start_index)
			return;
		st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
		if (st->codecpar->codec_type == codec_type) {
			/* check that parameters are OK */
			switch (codec_type) {
			case AVMEDIA_TYPE_AUDIO:
				if (st->codecpar->sample_rate != 0 &&
					st->codecpar->channels != 0)
					goto the_end;
				break;
			case AVMEDIA_TYPE_VIDEO:
			case AVMEDIA_TYPE_SUBTITLE:
				goto the_end;
			default:
				break;
			}
		}
	}
the_end:
	if (p && stream_index != -1)
		stream_index = p->stream_index[stream_index];
	av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
		av_get_media_type_string((AVMediaType)codec_type),
		old_index,
		stream_index);

	stream_component_close(is, old_index);
	stream_component_open(is, stream_index);
}


static void toggle_full_screen(VideoState *is)
{
	is->avp->is_full_screen = !is->avp->is_full_screen;
	SDL_SetWindowFullscreen(is->avp->window, is->avp->is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void toggle_audio_display(VideoState *is)
{
	int next = is->show_mode;
	do {
		next = (next + 1) % SHOW_MODE_NB;
	} while (next != is->show_mode && (next == SHOW_MODE_VIDEO && !is->video_st || next != SHOW_MODE_VIDEO && !is->audio_st));
	if (is->show_mode != next) {
		is->force_refresh = 1;
		is->show_mode = (ShowMode)next;
	}
}

static void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
	double remaining_time = 0.0;
	av_log(NULL, AV_LOG_INFO, "refresh_loop_wait_event enter...");
	while (!is->abort_request) {
		if (remaining_time > 0.0)
			av_usleep((int64_t)(remaining_time * 1000000.0));
		remaining_time = REFRESH_RATE;
		if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
			video_refresh(is, &remaining_time);
	}
	av_log(NULL, AV_LOG_INFO, "refresh_loop_wait_event left...");
}

static void seek_chapter(VideoState *is, int incr)
{
	int64_t pos = get_master_clock(is) * AV_TIME_BASE;
	int i;

	if (!is->ic->nb_chapters)
		return;

	/* find the current chapter */
	for (i = 0; i < is->ic->nb_chapters; i++) {
		AVChapter *ch = is->ic->chapters[i];
		if (av_compare_ts(pos, /*AV_TIME_BASE_Q*/{1,AV_TIME_BASE}, ch->start, ch->time_base) < 0) {
			i--;
			break;
		}
	}

	i += incr;
	i = FFMAX(i, 0);
	if (i >= is->ic->nb_chapters)
		return;

	av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
	stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
		/*AV_TIME_BASE_Q*/{ 1,AV_TIME_BASE }), 0, 0);
}

static int create_player_window(VideoState *is)
{
	VideoParam *avp = is->avp;
	int flags = 0;

	if (!avp->display_disable && !avp->vidFrameCallback)
	{
		flags = SDL_WINDOW_HIDDEN;
		if (avp->borderless)
			flags |= SDL_WINDOW_BORDERLESS;
		else
			flags |= SDL_WINDOW_RESIZABLE;
		if (avp->hWndPlayer != NULL)
		{
			avp->window = SDL_CreateWindowFrom(avp->hWndPlayer);
			if (avp->window)
			{
				SDL_SetWindowPosition(avp->window, 0, 0);
				SDL_SetWindowSize(avp->window, avp->default_width, avp->default_height);
			}
		}
		else
		{
			avp->window = SDL_CreateWindow("My Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, avp->default_width, avp->default_height, flags);
		}
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		if (avp->window) {
			avp->renderer = SDL_CreateRenderer(avp->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
			if (!avp->renderer) {
				av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
				avp->renderer = SDL_CreateRenderer(avp->window, -1, 0);
			}
			if (avp->renderer) {
				if (!SDL_GetRendererInfo(avp->renderer, &avp->renderer_info))
					av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", avp->renderer_info.name);
			}

			SDL_ShowWindow(avp->window);
		}
		if (!avp->window || !avp->renderer || !avp->renderer_info.num_texture_formats) {
			av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
			return -3;
		}
	}
	return 0;
}

static void do_stop(VideoState *is)
{
	if (!is) return;
	VideoParam *vp = is->avp;

	if (vp->renderer)
		SDL_DestroyRenderer(vp->renderer);
	if (vp->window)
		SDL_DestroyWindow(vp->window);

	vp->window = NULL;
	vp->renderer = NULL;
	vp->audio_dev = 0;
	vp->decoder_reorder_pts = -1;
	vp->file_iformat = NULL;
	vp->input_filename = NULL;
	vp->loop = 0;
	vp->start_time = 0;
}

/* handle an event sent by the GUI */
static void event_loop(VideoState *cur_stream)
{
	SDL_Event event;
	double incr, pos, frac;
	if (create_player_window(cur_stream) != 0) return;
	
	refresh_loop_wait_event(cur_stream, &event);

	av_log(NULL, AV_LOG_INFO, "Begin pause audio with id=%d.", cur_stream->avp->audio_dev);
	if (cur_stream->avp->audio_dev != 0)
	{
		SDL_PauseAudioDevice(cur_stream->avp->audio_dev, 1);
	}
	av_log(NULL, AV_LOG_INFO, "End pause audio with id=%d.", cur_stream->avp->audio_dev);

	av_log(NULL, AV_LOG_INFO, "Begin close stream.");
	stream_close(cur_stream);
	av_log(NULL, AV_LOG_INFO, "End close stream.");

	return;
}

static void saveFrameToJpg(AVFrame *frame, char* filePath)
{
	AVFormatContext *ofmtCtx = NULL;
	AVCodec *codec = NULL;
	AVCodecContext *codecCtx = NULL;
	AVStream *stream = NULL;
	int ret;

	/* allocate the output media context */
	avformat_alloc_output_context2(&ofmtCtx, NULL, NULL, filePath);
	if (!ofmtCtx) {
		return;
	}

	codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if (!codec) {
		return;
	}

	stream = avformat_new_stream(ofmtCtx, NULL);
	if (!stream) {
		return;
	}

	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) {
		return;
	}

	codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
	codecCtx->bit_rate = 400000;
	codecCtx->width = frame->width;
	codecCtx->height = frame->height;
	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = 25;

	/* open the codec */
	ret = avcodec_open2(codecCtx, codec, NULL);
	if (ret < 0) {
		return;
	}

	/* open the output file, if needed */
	ret = avio_open(&ofmtCtx->pb, filePath, AVIO_FLAG_WRITE);
	if (ret < 0) {
		return;
	}

	/* Write the stream header, if any. */
	ret = avformat_write_header(ofmtCtx, NULL);
	if (ret < 0) {
		return;
	}

	AVPacket avpkt = { 0 };
	int got_packet = 0;

	av_init_packet(&avpkt);

	/* encode the image */
	ret = avcodec_send_frame(codecCtx, frame);
	if (ret < 0) {
		return;
	}

	ret = avcodec_receive_packet(codecCtx, &avpkt);
	if (ret < 0) {
		return;
	}

	av_write_frame(ofmtCtx, &avpkt);

	av_write_trailer(ofmtCtx);

	/* Close the output file. */
	avio_closep(&ofmtCtx->pb);

	avcodec_free_context(&codecCtx);

	/* free the stream */
	avformat_free_context(ofmtCtx);

	return;
}

static void log_callback(void *ctx, int level, const char* fmt, va_list vl)
{
	int log_level = av_log_get_level();
	if (log_level == AV_LOG_QUIET || level > log_level || !pLogCallback) return;

	SYSTEMTIME sys;
	GetLocalTime(&sys);
	char szTime[1024] = { 0 };
	switch (level)
	{
	case AV_LOG_PANIC:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [PANIC] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_FATAL:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [FATAL] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_ERROR:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [ERROR] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_WARNING:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [WARNING] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_INFO:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [INFO] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_VERBOSE:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [VERBOSE] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_DEBUG:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [DEBUG] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	case 		AV_LOG_TRACE:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [TRACE] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
		break;
	default:
		sprintf(szTime, "[%4d-%02d-%02d %02d:%02d:%02d.%03d] [%d] ", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds, level);
		break;
	}
	int len = strlen(szTime);
	vsprintf(&szTime[len], fmt, vl);
	if (pLogCallback != NULL)
		pLogCallback(szTime);
}

//////////////////////////////////////////////////////////////////////////////////////////
void PL_InitRuntime()
{
	if (audio_Mutex == NULL)
	{
		audio_Mutex = SDL_CreateMutex();
		av_init_packet(&flush_pkt);
		flush_pkt.data = (uint8_t *)&flush_pkt;
	}
	//设置异常捕获
	SetUnhandledExceptionFilter(myExceptionProcessor);
}

void PL_DestoryRuntime()
{
	if (audio_Mutex != NULL)
	{
		SDL_DestroyMutex(audio_Mutex);
		audio_Mutex = NULL;
	}
}

HPLAYER PL_New(HWND videoContainer, char * audioDeviceName)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	av_register_all();
	avdevice_register_all();
	avformat_network_init();

	VideoState *is = (VideoState*)av_mallocz(sizeof(VideoState));
	if (!is) return NULL;

	VideoParam *avp = (VideoParam*)av_mallocz(sizeof(VideoParam));
	if (!avp)
	{
		av_free(is);
		return NULL;
	}
	init_video_parameters(avp);

	is->avp = avp;

	avp->audio_device_name = av_strdup(audioDeviceName);
	avp->hWndPlayer = videoContainer;

	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	return is;
}

int PL_Destory(HPLAYER player)
{
	if (player == NULL) return -1;
	
	VideoState *is = (VideoState*)player;
	if (is->event_loop_id != NULL) return -2;

	VideoParam *avp = is->avp;

	av_free(avp->audio_device_name);

	av_free(avp);
	av_free(is);

	return 0;
}

//设置播放结束时是否自动停止
int PL_SetAutoStopped(HPLAYER player, int autoStopped)
{
	VideoState *is = (VideoState*)player;
	if (!is) return -1;
	is->avp->autoexit = autoStopped;
	return 0;
}

void PL_SetPlayMode(HPLAYER player, int mode/*0-Video and Audio 1-Video only A-Audio only*/)
{
	VideoState *is = (VideoState*)player;
	if (!is) return;
	switch (mode)
	{
	case 1:
		is->avp->video_disable = is->avp->display_disable = 0;
		is->avp->audio_disable = 1;
		break;
	case 2:
		is->avp->video_disable = 1;
		is->avp->display_disable = 1;
		is->avp->audio_disable = 0;
		break;
	default:
		is->avp->video_disable = is->avp->display_disable = 0;
		is->avp->audio_disable = 0;
		break;
	}
}

void PL_SetLogCallback(int logLevel, fnLogCallBack callBack)
{
	av_log_set_level(logLevel);
	av_log_set_callback(log_callback);
	pLogCallback = callBack;
}

//设置自定义音频输出格式
int PL_SetAudioOutputFormat(HPLAYER player, int channels, int sampleRate)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;

	avp->customAudio_Channels = channels;
	avp->customAudio_Samplerate = sampleRate;

	return 0;
}

//设置视频显示是否保持比例
int PL_SetVideoKeepRatio(HPLAYER player, int keepRatio)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;

	avp->keepVideoRatio = keepRatio;
	return 0;
}

//设置自定义视频渲染回调函数
int PL_SetVideoFormatCallback(HPLAYER player, fnVideoFormatCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->vidFormatCallback = callBack;
	return 0;
}
int PL_SetVideoFrameCallback(HPLAYER player, fnVideoCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->vidFrameCallback = callBack;
	return 0;
}

//定义音频跳表回调函数
int PL_SetAudioMeterCallback(HPLAYER player, fnAudioMeterCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->audMeterCallback = callBack;
	return 0;
}

int PL_SetAudioFrameCallback(HPLAYER player, fnAudioCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->audFrameCallback = callBack;
	return 0;
}

int PL_SetPlayPositionCallback(HPLAYER player, fnPlayPosCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->playPosCallback = callBack;
	return 0;
}

int PL_SetPlayStateCallback(HPLAYER player, fnPlayStateCallBack callBack)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	avp->playStateCallback = callBack;
	return 0;
}

int PL_InitPlayer(HPLAYER player, int width, int height)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;

	//Init SDL runtime
	int flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
	if (avp->audio_disable)
	{
		flags &= ~SDL_INIT_AUDIO;
	}
	else
	{
		if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
			SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
	}

	if (avp->display_disable)
	{
		flags &= ~SDL_INIT_VIDEO;
	}

	if (SDL_Init(flags)) {
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		return -2;
	}

	avp->default_width = width;
	avp->default_height = height;

	SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);

	return 0;
}

int PL_Play(HPLAYER player, char *url, char *offsetTimeStamp, int staticFrame)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	
	if (is->event_loop_id != NULL) return -2;

	memset(is, 0, sizeof(VideoState));
	reset_video_parameters(avp);
	avp->screen_width = 0;
	avp->screen_height = 0;
	avp->audio_dev = 0;

	is->avp = avp;

	if (avp->hWndPlayer != NULL)
	{
		RECT rc;
		GetWindowRect(avp->hWndPlayer, &rc);
		avp->default_width = rc.right - rc.left;
		avp->default_height = rc.bottom - rc.top;
	}
	else
	{
		avp->default_width = 640;
		avp->default_height = 480;
	}

	avp->input_filename = av_strdup(url);
	av_parse_time(&avp->start_time, offsetTimeStamp, 1);

	is->step = staticFrame;
	is->abort_request = 0;

	if (avp->playStateCallback != NULL)
		avp->playStateCallback(is, PlayState_Opening);
	
	int ret = stream_open(is, avp->input_filename, avp->file_iformat);
	if (ret != 0) return -2;

	is->event_loop_id = SDL_CreateThread((SDL_ThreadFunction)event_loop, "Event_Loop", is);
	if (!is->event_loop_id)
		return -3;

	return 0;
}

//暂停文件、流
int PL_Pause(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	if (is->paused) return 0;

	toggle_pause(is);

	return 0;
}

//继续文件、流
int PL_Continue(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;

	if (!is->paused) return 0;

	toggle_pause(is);
	return 0;
}

int PL_SetStepPlay(HPLAYER player, int stepPlay)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;

	is->step = stepPlay;
	return 0;
}

EPlayState PL_GetPlayState(HPLAYER player)
{
	if (player == NULL) return PlayState_Error;
	VideoState *is = (VideoState*)player;
	if (is->event_loop_id == NULL) return PlayState_Stopped;

	return is->paused ? PlayState_Paused : PlayState_Playing;
}

int PL_Stop(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	if (is->event_loop_id == NULL) return 0;

	is->abort_request = 1;

	is->sampq.pktq->abort_request = 1;
	is->pictq.pktq->abort_request = 1;
	is->subpq.pktq->abort_request = 1;
	
	frame_queue_signal(&is->sampq);
	frame_queue_signal(&is->pictq);
	frame_queue_signal(&is->subpq);

	SDL_WaitThread(is->event_loop_id, NULL);

	do_stop(is);
	is->event_loop_id = NULL;
	is->read_tid = NULL;
	if (is->avp->playStateCallback)
	{
		is->avp->playStateCallback(is, PlayState_Stopped);
	}
	return 0;
}

int PL_StepFrames(HPLAYER player, int64_t frames)
{
	if (player == NULL || frames == 0) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	if (is->video_st == NULL || !is->ic) return -2;

	int ret_flag = 0;
	AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);
	double incr = av_rescale_q(frames, av_inv_q(frame_rate), /*AV_TIME_BASE_Q*/{ 1,AV_TIME_BASE }) / (double)AV_TIME_BASE;
	double pos = get_master_clock(is);
	if (isnan(pos))
	{
		pos = (double)is->seek_pos / AV_TIME_BASE;
	}
	pos += incr;
	if (is->ic->start_time != AV_NOPTS_VALUE && pos < is->ic->start_time / (double)AV_TIME_BASE)
	{
		pos = is->ic->start_time / (double)AV_TIME_BASE;
		ret_flag = 1;
	}
	else if ((int64_t)(pos * AV_TIME_BASE) > is->ic->duration)
	{
		pos = is->ic->duration / (double)AV_TIME_BASE;
		ret_flag = 2;
	}
	stream_seek(is, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);

	return ret_flag;
}

int64_t PL_GetPosition(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;

	double pos = get_master_clock(is);
	if (isnan(pos))
		return is->seek_pos / 1000;

	return (int64_t)(pos * 1000);
}

//清除缓存
int PL_ClearBuffer(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	packet_queue_flush(&is->videoq);
	packet_queue_flush(&is->audioq);
	packet_queue_flush(&is->subtitleq);
	return 0;
}

int PL_SetPosition(HPLAYER player, int64_t mseconds)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	if (!is->ic) return -2;

	double pos = mseconds * 1000;
	int ret_flag = 0;
	if (is->ic->start_time != AV_NOPTS_VALUE && pos < is->ic->start_time)
	{
		pos = is->ic->start_time;
		ret_flag = 1;
	}
	else if (pos >= is->ic->duration)
	{
		pos = is->ic->duration;
		ret_flag = 2;
	}

	stream_seek(is, (int64_t)(pos), 0, 0);

	return ret_flag;
}

//设置音量
int PL_SetVolume(HPLAYER player, int volume)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	int vol = volume;

	if (vol < 0)
		vol = 0;
	else if (vol > SDL_MIX_MAXVOLUME)
		vol = SDL_MIX_MAXVOLUME;

	is->avp->startup_volume = vol;
	is->audio_volume = vol;
	return 0;
}

//获取音量
int PL_GetVolume(HPLAYER player)
{
	if (player == NULL) return -1;
	VideoState *is = (VideoState*)player;
	VideoParam *avp = is->avp;
	return is->event_loop_id == NULL ? is->avp->startup_volume : is->audio_volume;
}

int PL_GetMediaInfo(char *url, CMediaInfo *pMediaInfo, char *thumbPathName, int thumbWidth, char* starttime)
{
	if (/*strchr(url, ";//") != NULL || */pMediaInfo == NULL) return -1;

	int ret, i;
	int audio_stream = -1, video_stream = -1;

	av_register_all();

	memset(pMediaInfo, 0, sizeof(CMediaInfo));

	AVFormatContext *ifmt_ctx = NULL;
	if ((ret = avformat_open_input(&ifmt_ctx, url, NULL, NULL)) < 0) {
		return ret;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
		return ret;
	}

	pMediaInfo->duration = ifmt_ctx->duration / 1000;
	int video_index = -1;

	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		AVStream *st;
		AVCodecContext *codec_ctx;
		st = ifmt_ctx->streams[i];
		enum AVMediaType type = st->codecpar->codec_type;
		if (type == AVMEDIA_TYPE_VIDEO)
		{
			pMediaInfo->width = st->codecpar->width;
			pMediaInfo->height = st->codecpar->height;
			video_index = i;
		}
		else if (type == AVMEDIA_TYPE_AUDIO)
		{
			pMediaInfo->samplerate = st->codecpar->sample_rate;
			pMediaInfo->channels = st->codecpar->channels;
		}
	}

	if (thumbPathName && thumbWidth > 0 && video_index >= 0)
	{
		AVStream *st = ifmt_ctx->streams[video_index];
		AVCodecContext *codec_ctx = st->codec;
		ret = avcodec_open2(codec_ctx,
			avcodec_find_decoder(codec_ctx->codec_id), NULL);
		if (ret < 0) {
			goto End;
		}

		int64_t pos = av_rescale_q(ifmt_ctx->duration / 20, /*AV_TIME_BASE_Q*/{1,AV_TIME_BASE}, st->time_base);
		if (starttime != NULL && strlen(starttime) > 0)
		{
			av_parse_time(&pos, starttime, 1);
		}

		avformat_seek_file(ifmt_ctx, video_index, INT64_MIN, pos, INT64_MAX, 0);

		AVPacket pkt;
		av_init_packet(&pkt);
		while (1)
		{
			ret = av_read_frame(ifmt_ctx, &pkt);
			if (ret < 0)
				break;
			if (pkt.stream_index == video_index)
			{
				AVFrame *frame = av_frame_alloc();
				avcodec_send_packet(codec_ctx, &pkt);
				if (avcodec_receive_frame(codec_ctx, frame) >= 0)
				{
					double sf = (double)thumbWidth / frame->width;
					struct SwsContext *sw = sws_getCachedContext(NULL,
						frame->width, frame->height, (AVPixelFormat)frame->format, thumbWidth, frame->height*sf,
						AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

					if (sw != NULL)
					{
						AVFrame *f = av_frame_alloc();
						avpicture_alloc((AVPicture *)f, AV_PIX_FMT_YUV420P, thumbWidth, frame->height*sf);
						f->width = thumbWidth;
						f->height = frame->height*sf;
						f->format = AV_PIX_FMT_YUV420P;
						ret = sws_scale(sw, (const uint8_t * const *)frame->data, frame->linesize,
							0, frame->height, f->data, f->linesize);
						if (ret >= 0)
						{
							saveFrameToJpg(f, thumbPathName);
						}
						av_frame_free(&f);
						sws_freeContext(sw);
					}
					av_frame_free(&frame);
					break;
				}
				av_frame_free(&frame);
			}
		}
		avcodec_close(ifmt_ctx->streams[video_index]->codec);
	}

End:

	avformat_close_input(&ifmt_ctx);
	return 0;
}
