//////////////////////////////////////////////////////////////////////
// RawSource - reads raw video data files
// Pech�EErnst - 2005
//////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <io.h>
#include "FCNTL.H"
//#include <stdlib.h>
#include "windows.h"
#include "avisynth.h"

#pragma warning(disable:4996)

#define MAX_WIDTH 4096
//#define MAX_RAWBUF MAX_WIDTH * 4
#define MAX_PIXTYPE_LEN 32
#define MAX_Y4M_HEADER 128
#define MIN_RESOLUTION 8
#define MIN_RESOLUTION 8

#define Y4M_STREAM_MAGIC "YUV4MPEG2"
#define Y4M_STREAM_MAGIC_LEN 9
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_FRAME_MAGIC_LEN 5
#define Y4M_LINE_TERMINATOR '\n'

class RawSource: public IClip {

    VideoInfo vi;
    int h_rawfile; //file handle for 64bit operations
    __int64 filelen;
    __int64 headeroffset; //YUV4MPEG2 header offset
    int headerlen; //YUV4MPEG2 frame header length
    char headerbuf[MAX_Y4M_HEADER];
    char pixel_type[MAX_PIXTYPE_LEN];
    unsigned char *rawbuf;
    int ret;
    int mapping[4];
    int mapcnt;
    int framesize;
    int level;
    bool show;    //show debug info

    struct ri_struct {
        int framenr;
        __int64 bytepos;
    };
    struct i_struct {
        __int64 index;
        char type; //Key, Delta, Bigdelta
    };

    ri_struct * rawindex;
    i_struct * index;

public:
    RawSource(const char name[], const int _width, const int _height,
              const char _pixel_type[], const int _fpsnum, const int _fpsden,
              const char indexstring[], const bool _show, IScriptEnvironment* env) ;
    virtual ~RawSource();
    int ParseHeader();

// avisynth virtual functions
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n);
    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) {};
    const VideoInfo& __stdcall GetVideoInfo();
    void __stdcall SetCacheHints(int cachehints,int frame_range) {};
};

RawSource::RawSource (const char name[], const int _width, const int _height,
                      const char _pixel_type[], const int _fpsnum, const int _fpsden,
                      const char indexstring[], const bool _show, IScriptEnvironment* env)
{
    memset(&vi, 0, sizeof(vi));

    if ((h_rawfile = _open(name, _O_BINARY | _O_RDONLY)) == -1)
        env->ThrowError("Cannot open videofile.");

    level = 0;
    show = _show;

    headerlen = 0;
    headeroffset = 0;
    filelen = _filelengthi64(h_rawfile);
    if (filelen == -1L)
        env->ThrowError("Cannot get videofile length.");

    strcpy(pixel_type, _pixel_type);

    vi.width = _width;
    vi.height = _height;
    vi.fps_numerator = _fpsnum;
    vi.fps_denominator = _fpsden;
    vi.SetFieldBased(false);

    if (!strcmp(indexstring, "")) {    //use header if valid else width, height, pixel_type from AVS are used
        ret = _read(h_rawfile, headerbuf, MAX_Y4M_HEADER);    //read some bytes and test on header
        ret = RawSource::ParseHeader();
        if (vi.width > MAX_WIDTH)
            env->ThrowError("Width too big(%d). Maximum acceptable width is %d.", vi.width, MAX_WIDTH);
        switch (ret) {
            case 4444:
                strncpy(pixel_type, "AYUV", 4);
                break;
            case 444:
                strncpy(pixel_type, "I444", 4);
                break;
            case 422:
                strncpy(pixel_type, "I422", 4);
                break;
            case 420:
                strncpy(pixel_type, "I420", 4);
                break;
            case 411:
                strncpy(pixel_type, "I411", 4);
                break;
            case 8:
                strncpy(pixel_type, "GRAY", 4);
                break;
            case -1:
                env->ThrowError("YUV4MPEG2 header error.");
                break;
            case -2:
                env->ThrowError("This file's YUV4MPEG2 HEADER is unsupported.");
                break;
            default:
                break;
        }
    }

    if (!stricmp(pixel_type, "RGBA") || !stricmp(pixel_type, "RGB32")) {
        vi.pixel_type = VideoInfo::CS_BGR32;
        mapping[0] = 2;
        mapping[1] = 1;
        mapping[2] = 0;
        mapping[3] = 3;
        mapcnt = 4;
    } else if (!stricmp(pixel_type, "BGRA") || !stricmp(pixel_type, "BGR32")) {
        vi.pixel_type = VideoInfo::CS_BGR32;
        mapping[0] = 0;
        mapping[1] = 1;
        mapping[2] = 2;
        mapping[3] = 3;
        mapcnt = 4;
    } else if (!stricmp(pixel_type,"ARGB")) {
        vi.pixel_type = VideoInfo::CS_BGR32;
        mapping[0] = 3;
        mapping[1] = 2;
        mapping[2] = 1;
        mapping[3] = 0;
        mapcnt = 4;
    } else if (!stricmp(pixel_type, "ABGR")) {
        vi.pixel_type = VideoInfo::CS_BGR32;
        mapping[0] = 1;
        mapping[1] = 2;
        mapping[2] = 3;
        mapping[3] = 0;
        mapcnt = 4;
    } else if (!stricmp(pixel_type, "RGB")) {
        vi.pixel_type = VideoInfo::CS_BGR24;
        mapping[0] = 2;
        mapping[1] = 1;
        mapping[2] = 0;
        mapcnt = 3;
    } else if (!stricmp(pixel_type, "BGR")) {
        vi.pixel_type = VideoInfo::CS_BGR24;
        mapping[0] = 0;
        mapping[1] = 1;
        mapping[2] = 2;
        mapcnt = 3;
    } else if (!stricmp(pixel_type, "YUYV") || !stricmp(pixel_type, "YUY2")) {
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapping[0] = 0;
        mapping[1] = 1;
        mapcnt = 2;
    } else if (!stricmp(pixel_type, "UYVY")) {
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapping[0] = 1;
        mapping[1] = 0;
        mapcnt = 2;
    } else if (!stricmp(pixel_type, "YVYU")) {
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapping[0] = 0;
        mapping[1] = 3;
        mapping[2] = 2;
        mapping[3] = 1;
        mapcnt = 4;
    } else if (!stricmp(pixel_type,"VYUY")) {
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapping[0] = 1;
        mapping[1] = 2;
        mapping[2] = 3;
        mapping[3] = 0;
        mapcnt = 4;
    } else if (!stricmp(pixel_type, "YV16") || !stricmp(pixel_type, "I422")) {
    // YV16:planar YVU 422 | I422:planar YUV 422
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapcnt = 4;
    } else if (!stricmp(pixel_type, "YV411") || !stricmp(pixel_type, "Y41B")  ||
               !stricmp(pixel_type, "I411")) {
    // Planar YUV411 format, write to YUY2 by duplicating chroma. (Added by Chikuzen)
        vi.pixel_type = VideoInfo::CS_YUY2;
        mapcnt = 4;
    // I couldn't find any sample to test this code.
    // Is there a person who has the Y411 sample ? (20110523 Chikuzen)
    //} else if (!stricmp(pixel_type, "Y411") || !stricmp(pixel_type, "IYU1")) {
    // UYYVYY411 Packed
    //    vi.pixel_type = VideoInfo::CS_YUY2;
    } else if (!stricmp(pixel_type, "I420") || !stricmp(pixel_type, "IYUV")) {
        vi.pixel_type = VideoInfo::CS_I420;
        mapping[0] = PLANAR_Y;
        mapping[1] = PLANAR_U;
        mapping[2] = PLANAR_V;
        mapcnt = 3;
    } else if (!stricmp(pixel_type, "YV12")) {
        vi.pixel_type = VideoInfo::CS_YV12;
        mapping[0] = PLANAR_Y;
        mapping[1] = PLANAR_V;
        mapping[2] = PLANAR_U;
        mapcnt = 3;
    } else if (!stricmp(pixel_type, "Y8") || !stricmp(pixel_type, "GRAY")) {
    //planar Y8 format (luma only), write to YV12 as grey. (Added by Fizick)
        if (!(vi.width & 1) && !(vi.height & 1)) {
            vi.pixel_type = VideoInfo::CS_YV12;
            mapping[0] = PLANAR_Y;
            mapping[1] = PLANAR_U;
            mapping[2] = PLANAR_V;
            mapcnt = 3;
        } else
            env->ThrowError("unsupported resolution. width and height must be even.");
    } else if (!stricmp(pixel_type, "NV12")) {
    //planar YUV420 (2plane, interleaved chroma), write to YV12. (Added by Chikuzen)
        vi.pixel_type = VideoInfo::CS_I420;
        mapping[0] = PLANAR_Y;
        mapping[1] = PLANAR_U;
        mapping[2] = PLANAR_V;
        mapcnt = 2;
    } else if (!stricmp(pixel_type, "NV21")) {
        vi.pixel_type = VideoInfo::CS_YV12;
        mapping[0] = PLANAR_Y;
        mapping[1] = PLANAR_V;
        mapping[2] = PLANAR_U;
        mapcnt = 2;
    } else
        env->ThrowError("Invalid pixel type. Supported: RGB, RGBA, BGR, BGRA, ARGB,"
                        " ABGR, YUY2, YUYV, UYVY, YVYU, VYUY, YV16, I422, YV411,"
                        " I411, YV12, I420, IYUV, NV12, NV21, Y8, GRAY");

    if (!stricmp(pixel_type, "Y8") || !stricmp(pixel_type, "GRAY"))
        framesize = vi.width * vi.height;
    else if (!stricmp(pixel_type, "I411") || !stricmp(pixel_type, "Y41B") ||
             !stricmp(pixel_type, "YV411") /*|| !stricmp(pixel_type, "Y411") ||
             !stricmp(pixel_type, "IYU1") */)
        framesize = (vi.width * vi.height *3) >> 1;
    else
        framesize = (vi.width * vi.height * vi.BitsPerPixel()) >> 3;

    int maxframe = (int)(filelen / (__int64)framesize);    //1 = one frame

    if (maxframe < 1)
        env->ThrowError("File too small for even one frame.");

    index = new i_struct[maxframe + 1];
    rawindex = new ri_struct[maxframe + 1];
    rawbuf = new unsigned char[vi.IsPlanar() ? vi.width : vi.width * (vi.BitsPerPixel() >> 3)];

//index build using string descriptor
    char * indstr;
    FILE * indexfile;
    char seps[] = " \n";
    char * token;
    int frame;
    __int64 bytepos;
    int p_ri = 0;
    int rimax;
    char * p_del;
    int num1, num2;
    int delta;    //delta between 1 frame
    int big_delta;    //delta between many e.g. 25 frames
    int big_frame_step;    //how many frames is big_delta for?
    int big_steps;    //how many big deltas have occured

//read all framenr:bytepos pairs
    if (!strcmp(indexstring, "")) {
        rawindex[0].framenr = 0;
        rawindex[0].bytepos = headeroffset;
        rawindex[1].framenr = 1;
        rawindex[1].bytepos = headeroffset + headerlen + framesize;
        rimax = 1;
    } else {
        const char * pos = strchr(indexstring, '.');
        if (pos != NULL) {    //assume indexstring is a filename
            indexfile = fopen(indexstring, "r");
            if (indexfile == 0)
                env->ThrowError("Cannot open indexfile.");
            fseek(indexfile, 0, SEEK_END);
            ret = ftell(indexfile);
            indstr = new char[ret + 1];
            fseek(indexfile, 0, SEEK_SET);
            fread(indstr, 1, ret, indexfile);
            fclose(indexfile);
        } else {
            indstr = new char[strlen(indexstring) + 1];
            strcpy(indstr, indexstring);
        }
        token = strtok(indstr, seps);
        while (token) {
            num1 = -1;
            num2 = -1;
            p_del = strchr(token, ':');
            if (!p_del)
                break;
            ret = sscanf(token, "%d", &num1);
            ret = sscanf(p_del + 1, "%d", &num2);

            if ((num1 < 0) || (num2 < 0))
                break;
            rawindex[p_ri].framenr = num1;
            rawindex[p_ri].bytepos = num2;
            p_ri++;
            token = strtok(0, seps);
        }
        rimax = p_ri - 1;
        delete indstr;
    }

    if ((rimax < 0) || rawindex[0].framenr)
        env->ThrowError("When using an index: frame 0 is mandatory");    //at least an entries for frame0

//fill up missing bytepos (create full index)
    frame = 0;    //framenumber
    p_ri = 0;    //pointer to raw index
    big_frame_step = 0;
    big_steps = 0;
    big_delta = 0;

    delta = framesize;
    //rawindex[1].bytepos - rawindex[0].bytepos;    //current bytepos delta
    bytepos = rawindex[0].bytepos;
    index[frame].type = 'K';
    while ((frame < maxframe) && ((bytepos + framesize) <= filelen)) {    //next frame must be readable
        index[frame].index = bytepos;

        if ((p_ri < rimax) && (rawindex[p_ri].framenr <= frame)) {
            p_ri++;
            big_steps = 1;
        }
        frame++;

        if ((p_ri > 0) && (rawindex[p_ri - 1].framenr + big_steps * big_frame_step == frame)) {
            bytepos = rawindex[p_ri - 1].bytepos + big_delta * big_steps;
            big_steps++;
            index[frame].type = 'B';
        } else {
            if (rawindex[p_ri].framenr == frame) {
                bytepos = rawindex[p_ri].bytepos;    //sync if framenumber is given in raw index
                index[frame].type = 'K';
            } else {
                bytepos = bytepos + delta;
                index[frame].type = 'D';
            }
        }

//check for new delta and big_delta
        if ((p_ri > 0) && (rawindex[p_ri].framenr == rawindex[p_ri-1].framenr + 1)) {
            delta = (int)(rawindex[p_ri].bytepos - rawindex[p_ri - 1].bytepos);
        } else if (p_ri > 1) {
//if more than 1 frame difference and
//2 successive equal distances then remember as big_delta
//if second delta < first delta then reset
            if (rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr == rawindex[p_ri - 1].framenr - rawindex[p_ri - 2].framenr) {
                big_frame_step = rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr;
                big_delta = (int)(rawindex[p_ri].bytepos - rawindex[p_ri - 1].bytepos);
            } else {
                if ((rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr) < (rawindex[p_ri - 1].framenr - rawindex[p_ri - 2].framenr)) {
                    big_delta = 0;
                    big_frame_step = 0;
                }
                if (frame >= rawindex[p_ri].framenr) {
                    big_delta = 0;
                    big_frame_step = 0;
                }
            }
        }
        frame = frame;
    }
    vi.num_frames = frame;
}


RawSource::~RawSource() {
    _close(h_rawfile);
    delete [] index;
    delete [] rawindex;
    delete [] rawbuf;
}

PVideoFrame __stdcall RawSource::GetFrame(int n, IScriptEnvironment* env) {
    unsigned char* pdst;
    int i, j;
    int samples_per_line;
    int number_of_lines;
    int pitch;
    PVideoFrame dst;

    if (show && !level) {
    //output debug info - call Subtitle

        char message[255];
        sprintf(message, "%d : %I64d %c", n, index[n].index, index[n].type);

        const char* arg_names[11] = {0, 0, "x", "y", "font", "size", "text_color", "halo_color"};
        AVSValue args[8] = {this, AVSValue(message), 4, 12, "Arial", 15, 0xFFFFFF, 0x000000};

        level = 1;
        PClip resultClip = (env->Invoke("Subtitle", AVSValue(args, 8), arg_names )).AsClip();
        PVideoFrame src1 = resultClip->GetFrame(n, env);
        level = 0;
        return src1;
    //end debug
    }

    dst = env->NewVideoFrame(vi);
    ret = (int)_lseeki64(h_rawfile, index[n].index, SEEK_SET);
    if (ret == -1L)
        return dst;    //error. do nothing

    if (!stricmp(pixel_type,"Y8")) {
    //planar Y8 format (luma only), write to YV12 as grey. (Added by Fizick)
        samples_per_line = dst->GetRowSize(PLANAR_Y);
        number_of_lines = dst->GetHeight(PLANAR_Y);
        pdst = dst->GetWritePtr(PLANAR_Y);
        pitch = dst->GetPitch(PLANAR_Y);
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 128, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            memcpy(pdst, rawbuf, samples_per_line);
            pdst += pitch;
        }

        for (i = 1; i < 3; i++) {
            samples_per_line = dst->GetRowSize(mapping[i]);
            number_of_lines = dst->GetHeight(mapping[i]);
            pdst = dst->GetWritePtr(mapping[i]);
            pitch = dst->GetPitch(mapping[i]);
            memset(rawbuf, 128, samples_per_line);
            for (j = 0; j < number_of_lines; j++) {
                memcpy(pdst, rawbuf, samples_per_line);
                pdst += pitch;
            }
        }

    } else if (!stricmp(pixel_type, "NV12") || !stricmp(pixel_type, "NV21")) {
    //planar NV12/NV21 (YUV420, 2plane, interleaved chroma), write to YV12. (Added by Chikuzen)
        samples_per_line = dst->GetRowSize(PLANAR_Y);
        number_of_lines = dst->GetHeight(PLANAR_Y);
        pdst = dst->GetWritePtr(PLANAR_Y);
        pitch = dst->GetPitch(PLANAR_Y);
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            memcpy(pdst, rawbuf, samples_per_line);
            pdst += pitch;
        }

        number_of_lines >>= 1;
        pdst = dst->GetWritePtr(mapping[1]);
        pitch = dst->GetPitch(mapping[1]);
        unsigned char *pdst2 = dst->GetWritePtr(mapping[2]);
        int pitch2 = dst->GetPitch(mapping[2]);

        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            for (j = 0; j < (samples_per_line >> 1); j++) {
                pdst[j]  = rawbuf[j << 1];
                pdst2[j] = rawbuf[(j << 1) + 1];
            }
            pdst  += pitch;
            pdst2 += pitch2;
        }

    } else if (vi.IsPlanar()) { // YV12, I420 or IYUV
        for (i = 0; i < mapcnt; i++) {
            samples_per_line = dst->GetRowSize(mapping[i]);
            number_of_lines = dst->GetHeight(mapping[i]);
            pdst = dst->GetWritePtr(mapping[i]);
            pitch = dst->GetPitch(mapping[i]);
            for (j = 0; j < number_of_lines; j++) {
                memset(rawbuf, 0, samples_per_line);
                ret = _read(h_rawfile, rawbuf, samples_per_line);
                memcpy (pdst, rawbuf, samples_per_line);
                pdst += pitch;
            }
        }

    } else if (!stricmp(pixel_type, "YV16") || !stricmp(pixel_type, "I422")) {
    // convert to YUY2 by rearranging bytes
        samples_per_line = vi.width;
        number_of_lines = vi.height;
        pdst = dst->GetWritePtr();
        pitch = dst->GetPitch();
        //read and copy all luma lines
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            for (j = 0; j < samples_per_line; j++) {
                pdst[j << 1] = rawbuf[j];
            }
            pdst += pitch;
        }
        //read and copy all u/v lines
        samples_per_line >>= 1;
        int uv_offset = stricmp(pixel_type, "I422") ? 3 : 1;
        for (int k = 0; k < 2; k++) {
            pdst = dst->GetWritePtr() + uv_offset;
            for (i = 0; i < number_of_lines; i++) {
                memset(rawbuf, 0, samples_per_line);
                ret = _read(h_rawfile, rawbuf, samples_per_line);
                for (j = 0; j < samples_per_line; j++) {
                    pdst[j << 2] = rawbuf[j];
                }
                pdst += pitch;
            }
            uv_offset ^= 0x2; //switch u/v
        }

    } else if (!stricmp(pixel_type, "YV411") || !stricmp(pixel_type, "Y41B") ||
               !stricmp(pixel_type, "I411")) {
    // convert to YUY2 by rearranging bytes
        samples_per_line = vi.width;
        number_of_lines = vi.height;
        pdst = dst->GetWritePtr();
        pitch = dst->GetPitch();
        //read and copy all luma lines
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            for (j = 0; j < samples_per_line; j++) {
                pdst[j << 1] = rawbuf[j];
            }
            pdst += pitch;
        }
        //read and copy all u/v lines
        samples_per_line >>= 2;
        int uv_offset = stricmp(pixel_type, "I411") ? 3 : 1;
        for (int k = 0; k < 2; k++) {
            pdst = dst->GetWritePtr() + uv_offset;
            for (i = 0; i < number_of_lines; i++) {
                memset(rawbuf, 0, samples_per_line);
                ret = _read(h_rawfile, rawbuf, samples_per_line);
                for (j = 0; j < samples_per_line; j++) {
                    pdst[j << 3] = rawbuf[j];
                    pdst[(j << 3) + 4] = rawbuf[j]; //duplicate chroma
                }
                pdst += dst->GetPitch();
            }
            uv_offset ^= 0x2; //switch u/v
        }

    /*} else if (!stricmp(pixel_type, "Y411") || !stricmp(pixel_type, "IYU1")) {
        samples_per_line = vi.width * 3 >> 1;
        k = samples_per_line / 6;
        pdst = dst->GetWritePtr();
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, MAX_RAWBUF);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            for (j = 0; j < k; j++) {
                int l = j << 3;
                int m = j * 6;
                pdst[l]     = rawbuf[m + 1]; //Y
                pdst[l + 1] = rawbuf[m + 0]; //U
                pdst[l + 2] = rawbuf[m + 2]; //Y
                pdst[l + 3] = rawbuf[m + 3]; //V
                pdst[l + 4] = rawbuf[m + 4]; //Y
                pdst[l + 5] = rawbuf[m + 0]; //U(duplicate)
                pdst[l + 6] = rawbuf[m + 5]; //Y
                pdst[l + 7] = rawbuf[m + 3]; //V(duplicate)
            }
            pdst += dst->GetPitch();
        }*/

    } else {
    //interleaved formats
        samples_per_line = vi.width * (vi.BitsPerPixel() >> 3);
        number_of_lines = vi.height;
        pdst = dst->GetWritePtr();
        pitch = dst->GetPitch();
        //read and copy all lines
        for (i = 0; i < number_of_lines; i++) {
            memset(rawbuf, 0, samples_per_line);
            ret = _read(h_rawfile, rawbuf, samples_per_line);
            for (j = 0; j < samples_per_line / mapcnt; j++) {
                for (int k = 0; k < mapcnt; k++) {
                    pdst[j * mapcnt + k] = rawbuf[j * mapcnt + mapping[k]];
                }
            }
            pdst += pitch;
        }
    }
    return dst;
}

int RawSource::ParseHeader() {
    int i = Y4M_STREAM_MAGIC_LEN;
    unsigned int numerator = 0, denominator = 0;
    int colorspace = 420;
    char ctag[9] = {0};

    if (strncmp(headerbuf, Y4M_STREAM_MAGIC, Y4M_STREAM_MAGIC_LEN))
        return 0;

    vi.height = 0;
    vi.width = 0;

    while ((i < MAX_Y4M_HEADER - 2) && (headerbuf[i] != Y4M_LINE_TERMINATOR)) {
        if (!strncmp(headerbuf + i, " W", 2)) {
            sscanf(headerbuf + i + 2, "%d", &vi.width);

        } else if (!strncmp(headerbuf + i, " H", 2)) {
            sscanf(headerbuf + i + 2, "%d", &vi.height);

        } else if (!strncmp(headerbuf + i, " I", 2)) {
            if (headerbuf[i + 2] == 'm')
                return -2;
            else if (headerbuf[i + 2] == 't')
                vi.image_type = VideoInfo::IT_TFF;
            else if (headerbuf[i + 2] == 'b')
                vi.image_type = VideoInfo::IT_BFF;

        } else if (!strncmp(headerbuf + i, " F", 2)) {
            sscanf(headerbuf + i + 2, "%u:%u", &numerator, &denominator);
            if (numerator && denominator)
                vi.SetFPS(numerator, denominator);
            else
                return -1;

        } else if (!strncmp(headerbuf + i, " C", 2)) {
            sscanf(headerbuf + i + 2, "%s", ctag);
            if (!strncmp(ctag, "444alpha", 8))
                colorspace = 4444;
            else if (!strncmp(ctag, "444", 3))
                colorspace = 444;
            else if (!strncmp(ctag, "422", 3))
                colorspace = 422;
            else if (!strncmp(ctag, "411", 3))
                colorspace = 411;
            else if (!strncmp(ctag, "420", 3))
                colorspace =420;
            else if (!strncmp(ctag, "mono", 4))
                colorspace = 8;
            else
                return -1;
        }

        i++;
    }

    if (!numerator || !denominator || !vi.width || !vi.height)
        return -1;

    i++;

    if (strncmp(headerbuf + i, Y4M_FRAME_MAGIC, Y4M_FRAME_MAGIC_LEN))
        return -1;

    headeroffset = i;

    i += Y4M_FRAME_MAGIC_LEN;

    while ((i < MAX_Y4M_HEADER) && (headerbuf[i] != Y4M_LINE_TERMINATOR))
        i++;

    headerlen = i - (int)headeroffset + 1;
    headeroffset = headeroffset + headerlen;

    return colorspace;
}

bool __stdcall RawSource::GetParity(int n) {return vi.image_type == VideoInfo::IT_TFF;}
//void __stdcall RawSource::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
const VideoInfo& __stdcall RawSource::GetVideoInfo() {return vi;}
//void __stdcall RawSource::SetCacheHints(int cachehints,int frame_range) ;


AVSValue __cdecl Create_RawSource(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    if (!args[0].Defined())
        env->ThrowError("RawSource: No source specified");

    const char *source = args[0].AsString();
    const int width = args[1].AsInt(720);
    const int height = args[2].AsInt(576);
    const char *pix_type = args[3].AsString("YUY2");
    const int fpsnum = args[4].AsInt(25);
    const int fpsden = args[5].AsInt(1);
    const char *index = args[6].AsString("");
    const bool show = args[7].AsBool(false);

    if (width < MIN_RESOLUTION || height < MIN_RESOLUTION)
        env->ThrowError("RawSource: width and height need to be %d or higher.", MIN_RESOLUTION);
    if (width > MAX_WIDTH)
        env->ThrowError("RawSource: width needs to be %d or lower.", MAX_WIDTH);
    if (strlen(pix_type) > MAX_PIXTYPE_LEN - 1)
        env->ThrowError("RawSource: pixel_type needs to be %d chars or shorter.", MAX_PIXTYPE_LEN - 1);
    if (fpsnum < 1 || fpsden < 1)
        env->ThrowError("RawSource: fpsnum and fpsden need to be 1 or higher.");

    return new RawSource(source, width, height, pix_type, fpsnum, fpsden, index, show, env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
  env->AddFunction("RawSource","[file]s[width]i[height]i[pixel_type]s[fpsnum]i[fpsden]i[index]s[show]b",Create_RawSource,0);
  return 0;
}
