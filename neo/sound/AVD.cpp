/**
* Copyright (C) 2018 George Kalmpokis
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to
* do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software. As clarification, there
* is no requirement that the copyright notice and permission be included in
* binary distributions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#pragma hdrstop
#include "precompiled.h"

#include <../neo/sound/AVD.h>

#ifndef USE_OPENAL
bool DecodeXAudio(byte** audio,int* len, IXAudio2SourceVoice** pMusicSourceVoice,bool ext) {
	if ( *len <= 0) {
		return false;
	}
	int ret = 0;
	int avindx = 0;
	AVFormatContext*		fmt_ctx = avformat_alloc_context();
	AVCodec*				dec;
	AVCodecContext*			dec_ctx;
	AVPacket packet;
	SwrContext* swr_ctx = NULL;
	unsigned char *avio_ctx_buffer = NULL;
	av_register_all();
	avio_ctx_buffer = static_cast<unsigned char *>(av_malloc((size_t)*len));
	memcpy(avio_ctx_buffer, *audio, *len);
	AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, *len, 0, NULL, NULL, NULL, NULL);
	fmt_ctx->pb = avio_ctx;
	avformat_open_input(&fmt_ctx, "", NULL, NULL);

	if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
	{
		return false;
	}
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	avindx = ret;
	dec_ctx = fmt_ctx->streams[avindx]->codec;
	dec = avcodec_find_decoder(dec_ctx->codec_id);
	if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0)
	{

		return false;
	}
	bool hasplanar = false;
	AVSampleFormat dst_smp;
	if (dec_ctx->sample_fmt >= 5) {
		dst_smp = static_cast<AVSampleFormat> (dec_ctx->sample_fmt - 5);
		swr_ctx = swr_alloc_set_opts(NULL, dec_ctx->channel_layout, dst_smp, dec_ctx->sample_rate, dec_ctx->channel_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate, 0, NULL);
		int res = swr_init(swr_ctx);
		hasplanar = true;
	}
	WAVEFORMATEX voiceFormat = { 0 };
	int format_byte = 0;
	bool use_ext = false;
	if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_U8 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
		format_byte = 1;
	}
	else if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_S16 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_S16P) {
		format_byte = 2;
	}
	else if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_S32 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_S32P) {
		format_byte = 4;
	}
	else {
		//return false;
		format_byte = 4;
		use_ext = true;
	}
	voiceFormat.nSamplesPerSec = dec_ctx->sample_rate;
	voiceFormat.nChannels = dec_ctx->channels;
	voiceFormat.nAvgBytesPerSec = voiceFormat.nSamplesPerSec * format_byte * voiceFormat.nChannels;
	voiceFormat.nBlockAlign = format_byte * voiceFormat.nChannels;
	voiceFormat.wBitsPerSample = format_byte * 8;
	if (ext) {
		WAVEFORMATEXTENSIBLE exvoice = { 0 };
		voiceFormat.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		voiceFormat.cbSize = 22;
		exvoice.Format = voiceFormat;
		switch (voiceFormat.nChannels) {
		case 1:
			exvoice.dwChannelMask = SPEAKER_MONO;
			break;
		case 2:
			exvoice.dwChannelMask = SPEAKER_STEREO;
			break;
		case 4:
			exvoice.dwChannelMask = SPEAKER_QUAD;
			break;
		case 5:
			exvoice.dwChannelMask = SPEAKER_5POINT1_SURROUND;
			break;
		case 7:
			exvoice.dwChannelMask = SPEAKER_7POINT1_SURROUND;
			break;
		default:
			exvoice.dwChannelMask = SPEAKER_MONO;
			break;
		}
		exvoice.Samples.wReserved = 0;
		exvoice.Samples.wSamplesPerBlock = voiceFormat.wBitsPerSample;
		exvoice.Samples.wValidBitsPerSample = voiceFormat.wBitsPerSample;
		if (!use_ext) {
			exvoice.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		}
		else {
			exvoice.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		}
		soundSystemLocal.hardware.GetIXAudio2()->CreateSourceVoice(*&pMusicSourceVoice, (WAVEFORMATEX *)&exvoice, XAUDIO2_VOICE_MUSIC);
	}
	else {
		if (!use_ext) {
			voiceFormat.wFormatTag = WAVE_FORMAT_PCM;
		}
		else {
			voiceFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
		voiceFormat.cbSize = 0;
		soundSystemLocal.hardware.GetIXAudio2()->CreateSourceVoice(*&pMusicSourceVoice, (WAVEFORMATEX *)&voiceFormat);
	}
	av_init_packet(&packet);
	AVFrame *frame;
	int frameFinished = 0;
	int offset = 0;
	int num_bytes = 0;
	int bufferoffset = format_byte * 10;
	unsigned long long length = *len;
	byte* tBuffer = (byte *)malloc(2 * length*bufferoffset);
	uint8_t** tBuffer2 = NULL;
	int  bufflinesize;

	while (av_read_frame(fmt_ctx, &packet) >= 0) {
		if (packet.stream_index == avindx) {
			frame = av_frame_alloc();
			frameFinished = 0;
			avcodec_decode_audio4(dec_ctx, frame, &frameFinished, &packet);
			if (frameFinished) {

				if (hasplanar) {
					av_samples_alloc_array_and_samples(&tBuffer2,
						&bufflinesize,
						voiceFormat.nChannels,
						av_rescale_rnd(frame->nb_samples, frame->sample_rate, frame->sample_rate, AV_ROUND_UP),
						dst_smp,
						0);

					int res = swr_convert(swr_ctx, tBuffer2, bufflinesize, (const uint8_t **)frame->extended_data, frame->nb_samples);
					num_bytes = av_samples_get_buffer_size(&bufflinesize, frame->channels,
						res, dst_smp, 1);
					memcpy(tBuffer + offset, tBuffer2[0], num_bytes);

					offset += num_bytes;
					av_freep(&tBuffer2[0]);

				}
				else {
					num_bytes = frame->linesize[0];
					memcpy(tBuffer + offset, frame->extended_data[0], num_bytes);
					offset += num_bytes;
				}



			}
			av_frame_free(&frame);
			free(frame);
		}

		av_free_packet(&packet);
	}
	*len = offset;
	*audio = (byte *)malloc(offset);
	memcpy(*audio, tBuffer, offset);
	free(tBuffer);
	tBuffer = NULL;
	if (swr_ctx != NULL) {
		swr_free(&swr_ctx);
	}

	avcodec_close(dec_ctx);

	av_free(fmt_ctx->pb);
	avformat_close_input(&fmt_ctx);


	av_free(avio_ctx->buffer);
	av_freep(avio_ctx);

	return true;
}
#else
bool DecodeALAudio(byte** audio, int* len, int *rate, ALenum *sample) {
	if ( *len <= 0) {
		return false;
	}
	int ret = 0;
	int avindx = 0;
	AVFormatContext*		fmt_ctx = avformat_alloc_context();
	AVCodec*				dec;
	AVCodecContext*			dec_ctx;
	AVPacket packet;
	SwrContext* swr_ctx = NULL;
	unsigned char *avio_ctx_buffer = NULL;
	av_register_all();
	avio_ctx_buffer = static_cast<unsigned char *>(av_malloc((size_t)*len));
	memcpy(avio_ctx_buffer, *audio, *len);
	AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, *len, 0, NULL, NULL, NULL, NULL);
	fmt_ctx->pb = avio_ctx;
	avformat_open_input(&fmt_ctx, "", NULL, NULL);

	if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
	{
		return false;
	}
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	avindx = ret;
	dec_ctx = fmt_ctx->streams[avindx]->codec;
	dec = avcodec_find_decoder(dec_ctx->codec_id);
	if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0)
	{

		return false;
	}
	bool hasplanar = false;
	AVSampleFormat dst_smp;
	if (dec_ctx->sample_fmt >= 5) {
		dst_smp = static_cast<AVSampleFormat> (dec_ctx->sample_fmt - 5);
		swr_ctx = swr_alloc_set_opts(NULL, dec_ctx->channel_layout, dst_smp, dec_ctx->sample_rate, dec_ctx->channel_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate, 0, NULL);
		int res = swr_init(swr_ctx);
		hasplanar = true;
	}
	int format_byte = 0;
	bool use_ext = false;
	if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_U8 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
		format_byte = 1;
	}
	else if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_S16 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_S16P) {
		format_byte = 2;
	}
	else if (dec_ctx->sample_fmt == AV_SAMPLE_FMT_S32 || dec_ctx->sample_fmt == AV_SAMPLE_FMT_S32P) {
		format_byte = 4;
	}
	else {
		//return false;
		format_byte = 4;
		use_ext = true;
	}
	*rate = dec_ctx->sample_rate;
	switch (format_byte) {
	case 1:
		if (dec_ctx->channels == 2) {
			*sample = AL_FORMAT_STEREO8;
		}
		else {
			*sample = AL_FORMAT_MONO8;
		}
		break;
	case 2:
		if (dec_ctx->channels == 2) {
			*sample = AL_FORMAT_STEREO16;
		}
		else {
			*sample = AL_FORMAT_MONO16;
		}
		break;
	case 4:
		if (dec_ctx->channels == 2) {
			*sample = AL_FORMAT_STEREO_FLOAT32;
		}
		else {
			*sample = AL_FORMAT_MONO_FLOAT32;
		}
		break;
	}
	av_init_packet(&packet);
	AVFrame *frame;
	int frameFinished = 0;
	int offset = 0;
	int num_bytes = 0;
	int bufferoffset = format_byte * 10;
	unsigned long long length = *len;
	byte* tBuffer = (byte *)malloc(2* length * bufferoffset);
	uint8_t** tBuffer2 = NULL;
	int  bufflinesize;
	while (av_read_frame(fmt_ctx, &packet) >= 0) {
		if (packet.stream_index == avindx) {
			frame = av_frame_alloc();
			frameFinished = 0;
			avcodec_decode_audio4(dec_ctx, frame, &frameFinished, &packet);
			if (frameFinished) {

				if (hasplanar) {
					av_samples_alloc_array_and_samples(&tBuffer2,
						&bufflinesize,
						frame->channels,
						av_rescale_rnd(frame->nb_samples, frame->sample_rate, frame->sample_rate, AV_ROUND_UP),
						dst_smp,
						0);
					int res = swr_convert(swr_ctx, tBuffer2, bufflinesize, (const uint8_t **)frame->extended_data, frame->nb_samples);
					num_bytes = av_samples_get_buffer_size(&bufflinesize, frame->channels,
						res, dst_smp, 1);
					memcpy(tBuffer + offset, tBuffer2[0], num_bytes);

					offset += num_bytes;
					av_freep(&tBuffer2[0]);

				}
				else {
					num_bytes = frame->linesize[0];
					while (!tBuffer) {
						tBuffer = (byte *)calloc(*len, sizeof(byte*));
					}
					memcpy(tBuffer + offset, frame->extended_data[0], num_bytes);
					offset += num_bytes;
				}



			}
			av_frame_free(&frame);
			free(frame);
		}

		av_free_packet(&packet);
	}
	*len = offset;
	*audio = (byte *)malloc(offset);
	memcpy(*audio, tBuffer, offset);
	free(tBuffer);
	tBuffer = NULL;
	if (swr_ctx != NULL) {
		swr_free(&swr_ctx);
	}

	avcodec_close(dec_ctx);
#ifdef _WINDOWS
	av_free(fmt_ctx->pb);
	avformat_close_input(&fmt_ctx);
#else
	//GK: Let it leak let it leak because linux thing that it freeing twiice
	avformat_close_input(&fmt_ctx);
	avformat_free_context(fmt_ctx);
#endif


	av_free(avio_ctx->buffer);
	av_freep(avio_ctx);
	return true;
}
#endif