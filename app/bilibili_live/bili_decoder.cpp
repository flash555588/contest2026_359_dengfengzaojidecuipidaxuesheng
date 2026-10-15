/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_decoder.h"
#include "bili_sps.h"
#include <codec_api.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct decoder {
  ISVCDecoder *codec;
  bili_frame_fn frame;
  bili_timed_frame_fn timed_frame;
  void *opaque;
  unsigned char *outputs[4][3];
  unsigned output_slot;
};
extern "C" int bili_mc_set_features(unsigned);
extern "C" int bili_decoder_set_mc_features(unsigned features)
{
  return features <= 3 ? bili_mc_set_features(features) : -EINVAL;
}
static void *create_decoder(bili_frame_fn frame, bili_timed_frame_fn timed, void *opaque, int threads)
{
  decoder *d = static_cast<decoder *>(calloc(1, sizeof(decoder)));
  SDecodingParam param;
  if (!d) return NULL;
  memset(&param, 0, sizeof(param));
  param.eEcActiveIdc = ERROR_CON_DISABLE;
  param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  if (WelsCreateDecoder(&d->codec) || !d->codec) {free(d);return NULL;}
  /* OpenH264 requires thread count before Initialize allocates contexts. */
  if(d->codec->SetOption(DECODER_OPTION_NUM_OF_THREADS,&threads) || d->codec->Initialize(&param)) {
    if (d->codec) WelsDestroyDecoder(d->codec);
    free(d); return NULL;
  }
  /* Keep the reference-picture budget bounded on the 32 MiB board. */
  d->frame = frame; d->timed_frame = timed; d->opaque = opaque;
  return d;
}
extern "C" void *bili_decoder_create(bili_frame_fn frame, void *opaque)
{
  return create_decoder(frame,NULL,opaque,0);
}
extern "C" void *bili_decoder_create_timed(bili_timed_frame_fn frame, void *opaque)
{
  return create_decoder(NULL,frame,opaque,0);
}
extern "C" int bili_decoder_feed(void *ctx, const uint8_t *data, size_t size)
{
  return bili_decoder_feed_timestamp(ctx, data, size, 0);
}
extern "C" int bili_decoder_feed_timestamp(void *ctx, const uint8_t *data, size_t size, uint64_t timestamp)
{
  decoder *d = static_cast<decoder *>(ctx);
  /* Inspect every in-band SPS before OpenH264 allocates reference pictures. */
  for (size_t i = 0; i + 4 < size; i++) {
    if (!memcmp(data + i, "\0\0\0\1", 4) && (data[i + 4] & 31) == 7) {
      size_t end = i + 5;
      while (end + 4 <= size && memcmp(data + end, "\0\0\0\1", 4)) end++;
      if (end + 4 > size) end = size;
      int w, h;
      int r = bili_sps_dimensions(data + i + 4, end - i - 4, &w, &h);
      if (r < 0) return r;
    }
  }
  unsigned char **planes=d->outputs[d->output_slot++ % 4];
  memset(planes,0,sizeof(d->outputs[0]));
  SBufferInfo info;
  memset(&info, 0, sizeof(info));
  info.uiInBsTimeStamp = timestamp;
  DECODING_STATE state = d->codec->DecodeFrameNoDelay(data, static_cast<int>(size), planes, &info);
  if (state & (dsOutOfMemory | dsRefListNullPtrs | dsDstBufNeedExpan)) return -ENOMEM;
  if (state & (dsBitstreamError | dsNoParamSets | dsInvalidArgument)) return -EPROTO;
  if (!info.iBufferStatus) return 0;
  const SSysMEMBuffer &b = info.UsrData.sSystemBuffer;
  if (b.iWidth < 2 || b.iHeight < 2 || b.iWidth > 1280 || b.iHeight > 720)
    return -EFBIG;
  if (!planes[0] || !planes[1] || !planes[2]) return -EPROTO;
  const uint8_t *readonly[3] = {planes[0], planes[1], planes[2]};
  return d->timed_frame ? d->timed_frame(d->opaque, readonly, b.iStride, b.iWidth, b.iHeight, info.uiOutYuvTimeStamp) :
         d->frame(d->opaque, readonly, b.iStride, b.iWidth, b.iHeight);
}
extern "C" int bili_decoder_flush(void *ctx)
{
  decoder *d = static_cast<decoder *>(ctx);
  int end = 1, remaining = 0;
  d->codec->SetOption(DECODER_OPTION_END_OF_STREAM, &end);
  d->codec->GetOption(DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &remaining);
  if (remaining < 0 || remaining > 32) return -EPROTO;
  for (int i=0; i<remaining; i++) {
    unsigned char *planes[3] = {NULL,NULL,NULL}; SBufferInfo info;
    memset(&info,0,sizeof(info));
    if (d->codec->FlushFrame(planes,&info) & (dsOutOfMemory|dsBitstreamError)) return -EPROTO;
    if (!info.iBufferStatus) continue;
    const SSysMEMBuffer &b=info.UsrData.sSystemBuffer;
    if (!planes[0] || !planes[1] || !planes[2] || b.iWidth>1280 || b.iHeight>720) return -EPROTO;
    const uint8_t *p[3]={planes[0],planes[1],planes[2]};
    int r=d->timed_frame ? d->timed_frame(d->opaque,p,b.iStride,b.iWidth,b.iHeight,info.uiOutYuvTimeStamp) :
      d->frame(d->opaque,p,b.iStride,b.iWidth,b.iHeight);
    if(r)return r;
  }
  return 0;
}
extern "C" void bili_decoder_destroy(void *ctx)
{
  decoder *d = static_cast<decoder *>(ctx);
  if (!d) return;
  /* The official destructor joins workers before releasing contexts.
   * Uninitialize() alone frees contexts while threaded decode is in flight. */
  WelsDestroyDecoder(d->codec); free(d);
}
