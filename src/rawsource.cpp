//////////////////////////////////////////////////////////////////////
// RawSource - reads raw video data files
// PechÅEErnst - 2005
//////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <io.h>
#include "FCNTL.H"
//#include <stdlib.h>
#include "windows.h"
#include "avisynth.h"

#pragma warning(disable:4996)

#define maxwidth 2880

class RawSource: public IClip {

	VideoInfo vi;
	int h_rawfile;	//file handle for 64bit operations
	__int64 filelen;
	__int64 headeroffset;	//YUV4MPEG2 header offset
	int headerlen;	//YUV4MPEG2 frame header length
	char headerbuf[64];
	char pixel_type[32];
	int bytes_per_line;
	unsigned char rawbuf[maxwidth*4];	//max linesize 4Bytes * maxwidth
	int ret;
	int mapping[4];
	int mapcnt;
	int maxframe;
	int framesize;

	int level;
	bool show;	//show debug info
	
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
	RawSource(const char name[], const int _width, const int _height, const char _pixel_type[], const char indexstring[], const bool _show, IScriptEnvironment* env) ;
	virtual ~RawSource();
	int ParseHeader();

// avisynth virtual functions
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
	bool __stdcall GetParity(int n);
	void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) {};
	const VideoInfo& __stdcall GetVideoInfo();
	void __stdcall SetCacheHints(int cachehints,int frame_range) {};
};

RawSource::RawSource (const char name[], const int _width, const int _height, const char _pixel_type[], const char indexstring[], const bool _show, IScriptEnvironment* env) {

	if (_width > maxwidth) env->ThrowError("Width too big.");

	h_rawfile = _open(name, _O_BINARY | _O_RDONLY);
	if (h_rawfile==-1) {
		env->ThrowError("Cannot open videofile.");
	}

	level = 0;
	show = _show;
	
	headerlen = 0;
	headeroffset = 0;
	filelen = _filelengthi64(h_rawfile);
	if (filelen==-1L) env->ThrowError("Cannot get videofile length.");

	strcpy(pixel_type, _pixel_type);

	vi.width = _width;
	vi.height = _height;
	vi.fps_numerator = 25;
	vi.fps_denominator = 1;
	vi.SetFieldBased(false);
	vi.audio_samples_per_second=0;

	if (!strcmp(indexstring, "")) {	//use header if valid else width, height, pixel_type from AVS are used
		ret = _read(h_rawfile, headerbuf, 60);	//read some bytes and test on header
		ret = RawSource::ParseHeader();
		if (ret>0) {
			strcpy(pixel_type,"I420");	//valid header found
		};
		if (ret<0) {
			env->ThrowError("YUV4MPEG2 header error.");
		};
	}

	vi.pixel_type = VideoInfo::CS_UNKNOWN;

	if (!stricmp(pixel_type,"RGB")) {
		vi.pixel_type = VideoInfo::CS_BGR24;
		mapping[0] = 2;
		mapping[1] = 1;
		mapping[2] = 0;
		mapcnt = 3;
	}

	if (!stricmp(pixel_type,"BGR")) {
		vi.pixel_type = VideoInfo::CS_BGR24;
		mapping[0] = 0;
		mapping[1] = 1;
		mapping[2] = 2;
		mapcnt = 3;
	}

	if (!stricmp(pixel_type,"RGBA")) {
		vi.pixel_type = VideoInfo::CS_BGR32;
		mapping[0] = 2;
		mapping[1] = 1;
		mapping[2] = 0;
		mapping[3] = 3;
		mapcnt = 4;
	}
	
	if (!stricmp(pixel_type,"BGRA")) {
		vi.pixel_type = VideoInfo::CS_BGR32;
		mapping[0] = 0;
		mapping[1] = 1;
		mapping[2] = 2;
		mapping[3] = 3;
		mapcnt = 4;
	}

	if (!stricmp(pixel_type,"YUYV")) {
		vi.pixel_type = VideoInfo::CS_YUY2;
		mapping[0] = 0;
		mapping[1] = 1;
		mapcnt = 2;
	}
	
	if (!stricmp(pixel_type,"UYVY")) {
		vi.pixel_type = VideoInfo::CS_YUY2;
		mapping[0] = 1;
		mapping[1] = 0;
		mapcnt = 2;
	}

	if (!stricmp(pixel_type,"YVYU")) {
		vi.pixel_type = VideoInfo::CS_YUY2;
		mapping[0] = 0;
		mapping[1] = 3;
		mapping[2] = 2;
		mapping[3] = 1;
		mapcnt = 4;
	}
	
	if (!stricmp(pixel_type,"VYUY")) {
		vi.pixel_type = VideoInfo::CS_YUY2;
		mapping[0] = 1;
		mapping[1] = 2;
		mapping[2] = 3;
		mapping[3] = 0;
		mapcnt = 4;
	}

	if (!stricmp(pixel_type,"YV16")) {
		vi.pixel_type = VideoInfo::CS_YUY2;
		mapcnt = 4;
	}

	if (!stricmp(pixel_type,"I420")) {
		vi.pixel_type = VideoInfo::CS_YV12;
		mapping[0] = 0;
		mapcnt = 1;
	}

	if (!stricmp(pixel_type,"YV12")) {
		vi.pixel_type = VideoInfo::CS_YV12;
		mapping[0] = 1;
		mapcnt = 1;
	}

	if (!stricmp(pixel_type,"Y8")) {
//planar Y8 format (luma only), write to YV12 as grey. (Added by Fizick)
		vi.pixel_type = VideoInfo::CS_YV12;
		mapcnt = 1;
	}

	if (vi.pixel_type == VideoInfo::CS_UNKNOWN) env->ThrowError("Invalid pixel type. Supported: RGB, RGBA, BGR, BGRA, YUYV, UYVY, YVYU, VYUY, YV16, YV12, I420, Y8");

	if (!stricmp(pixel_type,"Y8"))
		bytes_per_line = vi.width;
	else
		bytes_per_line = vi.width * vi.BitsPerPixel() / 8;
	framesize = bytes_per_line * vi.height;
	maxframe = (int)(filelen / (__int64)framesize);	//1 = one frame
	if (maxframe < 1) {
		env->ThrowError("File too small for even one frame.");
	}
	index = new i_struct[maxframe+1];
	rawindex = new ri_struct[maxframe+1];

//	index build using string descriptor
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
	int delta;	//delta between 1 frame
	int big_delta;	//delta between many e.g. 25 frames
	int big_frame_step;	//how many frames is big_delta for?
	int big_steps;	//how many big deltas have occured

//read all framenr:bytepos pairs
	if (!strcmp(indexstring, "")) {
		rawindex[0].framenr = 0;
		rawindex[0].bytepos = headeroffset;
		rawindex[1].framenr = 1;
		rawindex[1].bytepos = headeroffset + headerlen + framesize;
		rimax = 1;
	} else {
		const char * pos = strchr(indexstring, '.');
		if (pos != NULL) {	//assume indexstring is a filename
			indexfile = fopen(indexstring, "r");
			if (indexfile==0) env->ThrowError("Cannot open indexfile.");
			fseek(indexfile, 0, SEEK_END);
			ret = ftell(indexfile);
			indstr = new char[ret+1];
			fseek(indexfile, 0, SEEK_SET);
			fread(indstr, 1, ret, indexfile);
			fclose(indexfile);
		} else {
			indstr = new char[strlen(indexstring)+1];
			strcpy (indstr, indexstring);
		}
		token = strtok(indstr, seps);
		while (token != 0) {
			num1 = -1;
			num2 = -1;
			p_del = strchr(token, ':');
			if (!p_del) break;

			ret = sscanf(token, "%d", &num1);
			ret = sscanf(p_del+1, "%d", &num2);

			if ((num1<0)||(num2<0)) break;
			rawindex[p_ri].framenr = num1;
			rawindex[p_ri].bytepos = num2;
			p_ri++;
			token = strtok(0, seps);
		}
		rimax = p_ri-1;
		delete indstr;
	}
	if ((rimax<0) || (rawindex[0].framenr != 0)) {
		env->ThrowError("When using an index: frame 0 is mandatory");	//at least an entries for frame0
	}

//fill up missing bytepos (create full index)
	frame = 0;	//framenumber
	p_ri = 0;	//pointer to raw index
	big_frame_step = 0;
	big_steps = 0;
	big_delta = 0;

	delta = framesize;
	//rawindex[1].bytepos - rawindex[0].bytepos;	//current bytepos delta
	bytepos = rawindex[0].bytepos;
	index[frame].type = 'K';
	while ((frame<maxframe) && ((bytepos + framesize) <= filelen)) {	//next frame must be readable
		index[frame].index = bytepos;

		if ((p_ri<rimax) && (rawindex[p_ri].framenr<=frame)) {
			p_ri++;
			big_steps = 1;
		}
		frame++;

		if ((p_ri>0) && (rawindex[p_ri - 1].framenr + big_steps * big_frame_step == frame)) {
			bytepos = rawindex[p_ri - 1].bytepos + big_delta * big_steps;
			big_steps++;
			index[frame].type = 'B';
		} else {
			if (rawindex[p_ri].framenr == frame) {
				bytepos = rawindex[p_ri].bytepos;	//sync if framenumber is given in raw index
				index[frame].type = 'K';
			} else {
				bytepos = bytepos + delta;
				index[frame].type = 'D';
			}
		}

//check for new delta and big_delta		
		if ((p_ri>0) && (rawindex[p_ri].framenr == rawindex[p_ri-1].framenr + 1)) {
			delta = (int)(rawindex[p_ri].bytepos - rawindex[p_ri-1].bytepos);
		} else if (p_ri>1) {
//if more than 1 frame difference and
//2 successive equal distances then remember as big_delta
//if second delta < first delta then reset
			if (rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr == rawindex[p_ri - 1].framenr - rawindex[p_ri - 2].framenr) {
				big_frame_step = rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr;
				big_delta = (int)(rawindex[p_ri].bytepos - rawindex[p_ri-1].bytepos);
			} else {
				if (rawindex[p_ri].framenr - rawindex[p_ri - 1].framenr < rawindex[p_ri - 1].framenr - rawindex[p_ri - 2].framenr) {
					big_delta = 0;
					big_frame_step = 0;
				}
				if (frame>=rawindex[p_ri].framenr) {
					big_delta = 0;
					big_frame_step = 0;
				}
			}
		}
		frame=frame;
	}
	vi.num_frames = frame;
}


RawSource::~RawSource() {
	_close(h_rawfile);
	delete index;
	delete rawindex;
}

PVideoFrame __stdcall RawSource::GetFrame(int n, IScriptEnvironment* env) {

	unsigned char* pdst;
	int i;
	int j;
	int k;
	PVideoFrame dst;

	if (show && (level==0)) {
	//output debug info - call Subtitle

		char message[255];
		sprintf(message, "%d : %I64d %c", n, index[n].index, index[n].type );

		const char* arg_names[11] = {   0,                 0, "x", "y", "font", "size", "text_color", "halo_color"};
		AVSValue args[8]          = {this, AVSValue(message),   4,  12, "Arial",    15, 0xFFFFFF,         0x000000};

		level = 1;
		PClip resultClip = (env->Invoke("Subtitle",AVSValue(args,8), arg_names )).AsClip();
		PVideoFrame src1 = resultClip->GetFrame(n, env);
		level = 0;
		return src1;
	//end debug
	} 

	dst = env->NewVideoFrame(vi);
	ret = (int)_lseeki64(h_rawfile, index[n].index , SEEK_SET);
	if (ret==-1L) return dst;	//error. do nothing

	if (!stricmp(pixel_type,"Y8")) {
//planar Y8 format (luma only), write to YV12 as grey. (Added by Fizick)
		pdst = dst->GetWritePtr(PLANAR_Y);
		for (i=0; i<vi.height; i++) {
			memset(rawbuf, 128, maxwidth * 4);
			ret = _read(h_rawfile, rawbuf, vi.width);	//luma bytes
			memcpy (pdst, rawbuf, vi.width);
			pdst = pdst + dst->GetPitch(PLANAR_Y);
		}

		pdst = dst->GetWritePtr(PLANAR_U);
		memset(rawbuf, 128, maxwidth * 4);
		for (i=0; i<vi.height/2; i++) {
			memcpy (pdst, rawbuf, vi.width/2);
			pdst = pdst + dst->GetPitch(PLANAR_U);
		}

		pdst = dst->GetWritePtr(PLANAR_V);
		memset(rawbuf, 128, maxwidth * 4);
		for (i=0; i<vi.height/2; i++) {
			memcpy (pdst, rawbuf, vi.width/2);
			pdst = pdst + dst->GetPitch(PLANAR_V);
		}
	}
	else
	if (vi.IsPlanar()) {
//planar formats. Assuming 4 luma pixels with 1 chroma pair
		pdst = dst->GetWritePtr(PLANAR_Y);
		for (i=0; i<vi.height; i++) {
			memset(rawbuf, 0, maxwidth * 4);
			ret = _read(h_rawfile, rawbuf, vi.width);	//luma bytes
			memcpy (pdst, rawbuf, vi.width);
			pdst = pdst + dst->GetPitch(PLANAR_Y);
		}

		//switching between UV and VU
		if (mapping[0]==0) {
			pdst = dst->GetWritePtr(PLANAR_U);
		} else {
			pdst = dst->GetWritePtr(PLANAR_V);
		}
		for (i=0; i<vi.height/2; i++) {
			memset(rawbuf, 0, maxwidth * 4);
			ret = _read(h_rawfile, rawbuf, vi.width/2);	//chroma bytes
			memcpy (pdst, rawbuf, vi.width/2);
			pdst = pdst + dst->GetPitch(PLANAR_U);
		}

		if (mapping[0]==0) {
			pdst = dst->GetWritePtr(PLANAR_V);
		} else {
			pdst = dst->GetWritePtr(PLANAR_U);
		}
		for (i=0; i<vi.height/2; i++) {
			memset(rawbuf, 0, maxwidth * 4);
			ret = _read(h_rawfile, rawbuf, vi.width/2);	//chroma bytes
			memcpy (pdst, rawbuf, vi.width/2);
			pdst = pdst + dst->GetPitch(PLANAR_V);
		}

    } else if (!stricmp(pixel_type,"YV16")) {
// convert to YUY2 by rearranging bytes
		pdst = dst->GetWritePtr();
		//read and copy all luma lines
		for (i=0; i < vi.height; i++) {
				memset(rawbuf, 0, maxwidth * 4);
				ret = _read(h_rawfile, rawbuf, bytes_per_line/2);
				for (j=0; j<(bytes_per_line/4); j++) {
						pdst[j*4] = rawbuf[j*2];
						pdst[j*4+2] = rawbuf[j*2+1];
				}
				pdst = pdst + dst->GetPitch();
		}
		pdst = dst->GetWritePtr();
		//read and copy all U lines
		for (i=0; i < vi.height; i++) {
				ret = _read(h_rawfile, rawbuf, bytes_per_line/4);
				for (j=0; j<(bytes_per_line/4); j++) {
						pdst[j*4+1] = rawbuf[j]; // u-samples
				}
				pdst = pdst + dst->GetPitch();
		}
		pdst = dst->GetWritePtr();
		//read and copy all V lines
		for (int i=0; i < vi.height; i++) {
				ret = _read(h_rawfile, rawbuf, bytes_per_line/4);
				for (j=0; j<(bytes_per_line/4); j++) {
						pdst[j*4+3] = rawbuf[j]; // v-samples
				}
				pdst = pdst + dst->GetPitch();
		}
	} else {
//interleaved formats
		pdst = dst->GetWritePtr();

		//read and copy all lines
		for (i=0; i<vi.height; i++) {
			memset(rawbuf, 0, maxwidth * 4);
			ret = _read(h_rawfile, rawbuf, bytes_per_line);
			for (j=0; j<(bytes_per_line/mapcnt); j++) {
				for (k=0; k<mapcnt; k++) {
					pdst[j*mapcnt + k] = rawbuf[j*mapcnt + mapping[k]];
				}
			}
			pdst = pdst + dst->GetPitch();
		}
	}
	return dst;
}

int RawSource::ParseHeader() {
	int i,j;
	int numerator, denominator;
	if (memcmp(headerbuf, "YUV4MPEG2", 9) != 0) return 0;

	vi.height = 0;
	vi.width = 0;
	i=9;
	while ( (i<64) && (headerbuf[i]!='\n') ) {
		if (memcmp(headerbuf+i, " W", 2) == 0) {
			sscanf(headerbuf+i+2, "%d", &vi.width);
		}
		if (memcmp(headerbuf+i, " H", 2) == 0) {
			sscanf(headerbuf+i+2, "%d", &vi.height);
		}
		if (memcmp(headerbuf+i, " I", 2) == 0) {
			if (headerbuf[i+2]=='p') {
				vi.SetFieldBased(false);
			};
			if (headerbuf[i+2]=='t') {
				vi.SetFieldBased(true);
				vi.Set(VideoInfo::IT_TFF);
			}
			if (headerbuf[i+2]=='b') {
				vi.SetFieldBased(true);
				vi.Set(VideoInfo::IT_BFF);
			}
		}
		if (memcmp(headerbuf+i, " F", 2) == 0) {
			numerator = 0;
			denominator = 0;
			sscanf(headerbuf+i+2, "%d", &numerator);
			for (j=0; j<7; j++) {
				if (headerbuf[i+2+j]==':') {
					sscanf(headerbuf+i+2+j+1, "%d", &denominator);
				}
			}
			if ((numerator!=0) && (denominator!=0)) {
				vi.SetFPS(numerator, denominator);
			}
		}
		i++;
	}
	
	if ((numerator==0) || (denominator==0) || (vi.width==0) || (vi.height==0)) return -1;

	i++;
	if (memcmp(headerbuf+i, "FRAME", 5) != 0) return -1;
	headeroffset = i;
	i=i+5;
	while ( (i<64) && (headerbuf[i]!='\n') ) {
		i++;
	}
	headerlen = i - (int)headeroffset + 1;
	headeroffset = headeroffset + headerlen;
	return +1;
}

bool __stdcall RawSource::GetParity(int n) { return false; }
//void __stdcall RawSource::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
const VideoInfo& __stdcall RawSource::GetVideoInfo() { return vi;}
//void __stdcall RawSource::SetCacheHints(int cachehints,int frame_range) ;


AVSValue __cdecl Create_RawSource(AVSValue args, void* user_data, IScriptEnvironment* env) {
	return new RawSource(args[0].AsString(), args[1].AsInt(720), args[2].AsInt(576), args[3].AsString("YUY2"), args[4].AsString(""), args[5].AsBool(false), env);
}
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
  env->AddFunction("RawSource","[file]s[width]i[height]i[pixel_type]s[index]s[show]b",Create_RawSource,0);
  return 0;
}
