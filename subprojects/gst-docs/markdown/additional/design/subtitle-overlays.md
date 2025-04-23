# Subtitle Overlays and Hardware-Accelerated Playback

This document describes some of the considerations and requirements that
led to the current `GstVideoOverlayCompositionMeta` API which allows
attaching of subtitle bitmaps or logos to video buffers.

## Background

Subtitles can be muxed in containers or come from an external source.

Subtitles come in many shapes and colours. Usually they are either
text-based (incl. 'pango markup'), or bitmap-based (e.g. DVD subtitles
and the most common form of DVB subs). Bitmap based subtitles are
usually compressed in some way, like some form of run-length encoding.

Subtitles are currently decoded and rendered in subtitle-format-specific
overlay elements. These elements have two sink pads (one for raw video
and one for the subtitle format in question) and one raw video source
pad.

They will take care of synchronising the two input streams, and of
decoding and rendering the subtitles on top of the raw video stream.

Digression: one could theoretically have dedicated decoder/render
elements that output an AYUV or ARGB image, and then let a videomixer
element do the actual overlaying, but this is not very efficient,
because it requires us to allocate and blend whole pictures (1920x1080
AYUV = 8MB, 1280x720 AYUV = 3.6MB, 720x576 AYUV = 1.6MB) even if the
overlay region is only a small rectangle at the bottom. This wastes
memory and CPU. We could do something better by introducing a new format
that only encodes the region(s) of interest, but we don't have such a
format yet, and are not necessarily keen to rewrite this part of the
logic in playbin at this point - and we can't change existing elements'
behaviour, so would need to introduce new elements for this.

Playbin supports outputting compressed formats, i.e. it does not force
decoding to a raw format, but is happy to output to a non-raw format as
long as the sink supports that as well.

In case of certain hardware-accelerated decoding APIs, we will make use
of that functionality. However, the decoder will not output a raw video
format then, but some kind of hardware/API-specific format (in the caps)
and the buffers will reference hardware/API-specific objects that the
hardware/API-specific sink will know how to handle.

## The Problem

In the case of such hardware-accelerated decoding, the decoder will not
output raw pixels that can easily be manipulated. Instead, it will
output hardware/API-specific objects that can later be used to render a
frame using the same API.

Even if we could transform such a buffer into raw pixels, we most likely
would want to avoid that, in order to avoid the need to map the data
back into system memory (and then later back to the GPU). It's much
better to upload the much smaller encoded data to the GPU/DSP and then
leave it there until rendered.

Before `GstVideoOverlayComposition` playbin only supported subtitles on
top of raw decoded video. It would try to find a suitable overlay element
from the plugin registry based on the input subtitle caps and the rank.
(It is assumed that we will be able to convert any raw video format into
any format required by the overlay using a converter such as videoconvert.)

It would not render subtitles if the video sent to the sink is not raw
YUV or RGB or if conversions had been disabled by setting the
native-video flag on playbin.

Subtitle rendering is considered an important feature. Enabling
hardware-accelerated decoding by default should not lead to a major
feature regression in this area.

This means that we need to support subtitle rendering on top of non-raw
video.

## Possible Solutions

The goal is to keep knowledge of the subtitle format within the
format-specific GStreamer plugins, and knowledge of any specific video
acceleration API to the GStreamer plugins implementing that API. We do
not want to make the pango/dvbsuboverlay/dvdspu/kate plugins link to
libva/libvdpau/etc. and we do not want to make the vaapi/vdpau plugins
link to all of libpango/libkate/libass etc.

Multiple possible solutions come to mind:

1)  backend-specific overlay elements
    
    e.g. vaapitextoverlay, vdpautextoverlay, vaapidvdspu, vdpaudvdspu,
    vaapidvbsuboverlay, vdpaudvbsuboverlay, etc.
    
    This assumes the overlay can be done directly on the
    backend-specific object passed around.
    
    The main drawback with this solution is that it leads to a lot of
    code duplication and may also lead to uncertainty about distributing
    certain duplicated pieces of code. The code duplication is pretty
    much unavoidable, since making textoverlay, dvbsuboverlay, dvdspu,
    kate, assrender, etc. available in form of base classes to derive
    from is not really an option. Similarly, one would not really want
    the vaapi/vdpau plugin to depend on a bunch of other libraries such
    as libpango, libkate, libtiger, libass, etc.
    
    One could add some new kind of overlay plugin feature though in
    combination with a generic base class of some sort, but in order to
    accommodate all the different cases and formats one would end up
    with quite convoluted/tricky API.
    
    (Of course there could also be a `GstFancyVideoBuffer` that provides
    an abstraction for such video accelerated objects and that could
    provide an API to add overlays to it in a generic way, but in the
    end this is just a less generic variant of (c), and it is not clear
    that there are real benefits to a specialised solution vs. a more
    generic one).

2)  convert backend-specific object to raw pixels and then overlay
    
    Even where possible technically, this is most likely very
    inefficient.

3)  attach the overlay data to the backend-specific video frame buffers
    in a generic way and do the actual overlaying/blitting later in
    backend-specific code such as the video sink (or an accelerated
    encoder/transcoder)
    
    In this case, the actual overlay rendering (i.e. the actual text
    rendering or decoding DVD/DVB data into pixels) is done in the
    subtitle-format-specific GStreamer plugin. All knowledge about the
    subtitle format is contained in the overlay plugin then, and all
    knowledge about the video backend in the video backend specific
    plugin.
    
    The main question then is how to get the overlay pixels (and we will
    only deal with pixels here) from the overlay element to the video
    sink.
    
    This could be done in multiple ways: One could send custom events
    downstream with the overlay data, or one could attach the overlay
    data directly to the video buffers in some way.
    
    Sending inline events has the advantage that is fairly
    transparent to any elements between the overlay element and the
    video sink: if an effects plugin creates a new video buffer for the
    output, nothing special needs to be done to maintain the subtitle
    overlay information, since the overlay data is not attached to the
    buffer. However, it slightly complicates things at the sink, since
    it would also need to look for the new event in question instead of
    just processing everything in its buffer render function.
    
    If one attaches the overlay data to the buffer directly, any element
    between overlay and video sink that creates a new video buffer would
    need to be aware of the overlay data attached to it and copy it over
    to the newly-created buffer.
    
    One would have to do implement a special kind of new query (e.g.
    FEATURE query) that is not passed on automatically by
    `gst_pad_query_default()` in order to make sure that all elements
    downstream will handle the attached overlay data. (This is only a
    problem if we want to also attach overlay data to raw video pixel
    buffers; for new non-raw types we can just make it mandatory and
    assume support and be done with it; for existing non-raw types
    nothing changes anyway if subtitles don't work) (we need to maintain
    backwards compatibility for existing raw video pipelines like e.g.:
    `..decoder ! suboverlay ! encoder..`)
    
    Even though slightly more work, attaching the overlay information to
    buffers seems more intuitive than sending it interleaved as events.
    And buffers stored or passed around (e.g. via the "last-buffer"
    property in the sink when doing screenshots via playbin) always
    contain all the information needed.

4)  create a video/x-raw-delta format and use a backend-specific
    videomixer
    
    This possibility was hinted at already in the digression in section
    1. It would satisfy the goal of keeping subtitle format knowledge in
    the subtitle plugins and video backend knowledge in the video
    backend plugin. It would also add a concept that might be generally
    useful (think ximagesrc capture with xdamage). However, it would
    require adding foorender variants of all the existing overlay
    elements, and changing playbin to that new design, which is somewhat
    intrusive. And given the general nature of such a new format/API, we
    would need to take a lot of care to be able to accommodate all
    possible use cases when designing the API, which makes it
    considerably more ambitious. Lastly, we would need to write
    videomixer variants for the various accelerated video backends as
    well.

Overall (c) appears to be the most promising solution. It is the least
intrusive and should be fairly straight-forward to implement with
reasonable effort, requiring only small changes to existing elements and
requiring no new elements.

Doing the final overlaying in the sink as opposed to a videomixer or
overlay in the middle of the pipeline has other advantages:

  - if video frames need to be dropped, e.g. for QoS reasons, we could
    also skip the actual subtitle overlaying and possibly the
    decoding/rendering as well, if the implementation and API allows for
    that to be delayed.

  - the sink often knows the actual size of the window/surface/screen
    the output video is rendered to. This *may* make it possible to
    render the overlay image in a higher resolution than the input
    video, solving a long standing issue with pixelated subtitles on top
    of low-resolution videos that are then scaled up in the sink. This
    would require for the rendering to be delayed of course instead of
    just attaching an AYUV/ARGB/RGBA blog of pixels to the video buffer
    in the overlay, but that could all be supported.

  - if the video backend / sink has support for high-quality text
    rendering (clutter?) we could just pass the text or pango markup to
    the sink and let it do the rest (this is unlikely to be supported in
    the general case - text and glyph rendering is hard; also, we don't
    really want to make up our own text markup system, and pango markup
    is probably too limited for complex karaoke stuff).

## API needed

1)  Representation of subtitle overlays to be rendered
    
    We need to pass the overlay pixels from the overlay element to the
    sink somehow. Whatever the exact mechanism, let's assume we pass a
    refcounted `GstVideoOverlayComposition` struct or object.
    
    A composition is made up of one or more overlays/rectangles.
    
    In the simplest case an overlay rectangle is just a blob of
    RGBA/ABGR \[FIXME?\] or AYUV pixels with positioning info and other
    metadata, and there is only one rectangle to render.
    
    We're keeping the naming generic ("OverlayFoo" rather than
    "SubtitleFoo") here, since this might also be handy for other use
    cases such as e.g. logo overlays or so. It is not designed for
    full-fledged video stream mixing
        though.

```
        // Note: don't mind the exact implementation details, they'll be hidden
        
        // FIXME: might be confusing in 0.11 though since GstXOverlay was
        //        renamed to GstVideoOverlay in 0.11, but not much we can do,
        //        maybe we can rename GstVideoOverlay to something better
        
        struct GstVideoOverlayComposition
        {
            guint                          num_rectangles;
            GstVideoOverlayRectangle    ** rectangles;
        
            /* lowest rectangle sequence number still used by the upstream
             * overlay element. This way a renderer maintaining some kind of
             * rectangles <-> surface cache can know when to free cached
             * surfaces/rectangles. */
            guint                          min_seq_num_used;
        
            /* sequence number for the composition (same series as rectangles) */
            guint                          seq_num;
        }
        
        struct GstVideoOverlayRectangle
        {
            /* Position on video frame and dimension of output rectangle in
             * output frame terms (already adjusted for the PAR of the output
             * frame). x/y can be negative (overlay will be clipped then) */
            gint  x, y;
            guint render_width, render_height;
        
            /* Dimensions of overlay pixels */
            guint width, height, stride;
        
            /* This is the PAR of the overlay pixels */
            guint par_n, par_d;
        
            /* Format of pixels, GST_VIDEO_FORMAT_ARGB on big-endian systems,
             * and BGRA on little-endian systems (i.e. pixels are treated as
             * 32-bit values and alpha is always in the most-significant byte,
             * and blue is in the least-significant byte).
             *
             * FIXME: does anyone actually use AYUV in practice? (we do
             * in our utility function to blend on top of raw video)
             * What about AYUV and endianness? Do we always have [A][Y][U][V]
             * in memory? */
            /* FIXME: maybe use our own enum? */
            GstVideoFormat format;
        
            /* Refcounted blob of memory, no caps or timestamps */
            GstBuffer *pixels;
        
            // FIXME: how to express source like text or pango markup?
            //        (just add source type enum + source buffer with data)
            //
            // FOR 0.10: always send pixel blobs, but attach source data in
            // addition (reason: if downstream changes, we can't renegotiate
            // that properly, if we just do a query of supported formats from
            // the start). Sink will just ignore pixels and use pango markup
            // from source data if it supports that.
            //
            // FOR 0.11: overlay should query formats (pango markup, pixels)
            // supported by downstream and then only send that. We can
            // renegotiate via the reconfigure event.
            //
        
            /* sequence number: useful for backends/renderers/sinks that want
             * to maintain a cache of rectangles <-> surfaces. The value of
             * the min_seq_num_used in the composition tells the renderer which
             * rectangles have expired. */
            guint      seq_num;
        
            /* FIXME: we also need a (private) way to cache converted/scaled
             * pixel blobs */
        }
    
    (a1) Overlay consumer
        API:
    
        How would this work in a video sink that supports scaling of textures:
        
        gst_foo_sink_render () {
          /* assume only one for now */
          if video_buffer has composition:
            composition = video_buffer.get_composition()
        
            for each rectangle in composition:
              if rectangle.source_data_type == PANGO_MARKUP
                actor = text_from_pango_markup (rectangle.get_source_data())
              else
                pixels = rectangle.get_pixels_unscaled (FORMAT_RGBA, ...)
                actor = texture_from_rgba (pixels, ...)
        
              .. position + scale on top of video surface ...
        }
    
    (a2) Overlay producer
        API:
    
        e.g. logo or subpicture overlay: got pixels, stuff into rectangle:
        
         if (logoverlay->cached_composition == NULL) {
           comp = composition_new ();
        
           rect = rectangle_new (format, pixels_buf,
                                 width, height, stride, par_n, par_d,
                                 x, y, render_width, render_height);
        
           /* composition adds its own ref for the rectangle */
           composition_add_rectangle (comp, rect);
           rectangle_unref (rect);
        
           /* buffer adds its own ref for the composition */
           video_buffer_attach_composition (comp);
        
           /* we take ownership of the composition and save it for later */
           logoverlay->cached_composition = comp;
         } else {
           video_buffer_attach_composition (logoverlay->cached_composition);
         }
```
    
    FIXME: also add some API to modify render position/dimensions of a
    rectangle (probably requires creation of new rectangle, unless we
    handle writability like with other mini objects).

2)  Fallback overlay rendering/blitting on top of raw video
    
    Eventually we want to use this overlay mechanism not only for
    hardware-accelerated video, but also for plain old raw video, either
    at the sink or in the overlay element directly.
    
    Apart from the advantages listed earlier in section 3, this allows
    us to consolidate a lot of overlaying/blitting code that is
    currently repeated in every single overlay element in one location.
    This makes it considerably easier to support a whole range of raw
    video formats out of the box, add SIMD-optimised rendering using
    ORC, or handle corner cases correctly.
    
    (Note: side-effect of overlaying raw video at the video sink is that
    if e.g. a screnshotter gets the last buffer via the last-buffer
    property of basesink, it would get an image without the subtitles on
    top. This could probably be fixed by re-implementing the property in
    `GstVideoSink` though. Playbin2 could handle this internally as well).

```
        void
        gst_video_overlay_composition_blend (GstVideoOverlayComposition * comp
                                             GstBuffer                  * video_buf)
        {
          guint n;
        
          g_return_if_fail (gst_buffer_is_writable (video_buf));
          g_return_if_fail (GST_BUFFER_CAPS (video_buf) != NULL);
        
          ... parse video_buffer caps into BlendVideoFormatInfo ...
        
          for each rectangle in the composition: {
        
                 if (gst_video_format_is_yuv (video_buf_format)) {
                   overlay_format = FORMAT_AYUV;
                 } else if (gst_video_format_is_rgb (video_buf_format)) {
                   overlay_format = FORMAT_ARGB;
                 } else {
                   /* FIXME: grayscale? */
                   return;
                 }
        
                 /* this will scale and convert AYUV<->ARGB if needed */
                 pixels = rectangle_get_pixels_scaled (rectangle, overlay_format);
        
                 ... clip output rectangle ...
        
                 __do_blend (video_buf_format, video_buf->data,
                             overlay_format, pixels->data,
                             x, y, width, height, stride);
        
                 gst_buffer_unref (pixels);
          }
        }
```

3)  Flatten all rectangles in a composition
    
    We cannot assume that the video backend API can handle any number of
    rectangle overlays, it's possible that it only supports one single
    overlay, in which case we need to squash all rectangles into one.
    
    However, we'll just declare this a corner case for now, and
    implement it only if someone actually needs it. It's easy to add
    later API-wise. Might be a bit tricky if we have rectangles with
    different PARs/formats (e.g. subs and a logo), though we could
    probably always just use the code from (b) with a fully transparent
    video buffer to create a flattened overlay buffer.

4)  query support for the new video composition mechanism
        
    This is handled via `GstMeta` and an ALLOCATION query - we can simply
    query whether downstream supports the `GstVideoOverlayComposition` meta.
    
    There appears to be no issue with downstream possibly not being
    linked yet at the time when an overlay would want to do such a
    query, but we would just have to default to something and update
    ourselves later on a reconfigure event then.

Other considerations:

  - renderers (overlays or sinks) may be able to handle only ARGB or
    only AYUV (for most graphics/hw-API it's likely ARGB of some sort,
    while our blending utility functions will likely want the same
    colour space as the underlying raw video format, which is usually
    YUV of some sort). We need to convert where required, and should
    cache the conversion.

  - renderers may or may not be able to scale the overlay. We need to do
    the scaling internally if not (simple case: just horizontal scaling
    to adjust for PAR differences; complex case: both horizontal and
    vertical scaling, e.g. if subs come from a different source than the
    video or the video has been rescaled or cropped between overlay
    element and sink).

  - renderers may be able to generate (possibly scaled) pixels on demand
    from the original data (e.g. a string or RLE-encoded data). We will
    ignore this for now, since this functionality can still be added
    later via API additions. The most interesting case would be to pass
    a pango markup string, since e.g. clutter can handle that natively.

  - renderers may be able to write data directly on top of the video
    pixels (instead of creating an intermediary buffer with the overlay
    which is then blended on top of the actual video frame), e.g.
    dvdspu, dvbsuboverlay

However, in the interest of simplicity, we should probably ignore the
fact that some elements can blend their overlays directly on top of the
video (decoding/uncompressing them on the fly), even more so as it's not
obvious that it's actually faster to decode the same overlay 70-90 times
(say) (ie. ca. 3 seconds of video frames) and then blend it 70-90 times
instead of decoding it once into a temporary buffer and then blending it
directly from there, possibly SIMD-accelerated. Also, this is only
relevant if the video is raw video and not some hardware-acceleration
backend object.

And ultimately it is the overlay element that decides whether to do the
overlay right there and then or have the sink do it (if supported). It
could decide to keep doing the overlay itself for raw video and only use
our new API for non-raw video.

  - renderers may want to make sure they only upload the overlay pixels
    once per rectangle if that rectangle recurs in subsequent frames (as
    part of the same composition or a different composition), as is
    likely. This caching of e.g. surfaces needs to be done renderer-side
    and can be accomplished based on the sequence numbers. The
    composition contains the lowest sequence number still in use
    upstream (an overlay element may want to cache created
    compositions+rectangles as well after all to re-use them for
    multiple frames), based on that the renderer can expire cached
    objects. The caching needs to be done renderer-side because
    attaching renderer-specific objects to the rectangles won't work
    well given the refcounted nature of rectangles and compositions,
    making it unpredictable when a rectangle or composition will be
    freed or from which thread context it will be freed. The
    renderer-specific objects are likely bound to other types of
    renderer-specific contexts, and need to be managed in connection
    with those.

  - composition/rectangles should internally provide a certain degree of
    thread-safety. Multiple elements (sinks, overlay element) might
    access or use the same objects from multiple threads at the same
    time, and it is expected that elements will keep a ref to
    compositions and rectangles they push downstream for a while, e.g.
    until the current subtitle composition expires.

## Future considerations

  - alternatives: there may be multiple versions/variants of the same
    subtitle stream. On DVDs, there may be a 4:3 version and a 16:9
    version of the same subtitles. We could attach both variants and let
    the renderer pick the best one for the situation (currently we just
    use the 16:9 version). With totem, it's ultimately totem that adds
    the 'black bars' at the top/bottom, so totem also knows if it's got
    a 4:3 display and can/wants to fit 4:3 subs (which may render on top
    of the bars) or not, for example.

## Misc. FIXMEs

TEST: should these look (roughly) alike (note text distortion) - needs
fixing in textoverlay

```
gst-launch-1.0 \
   videotestsrc ! video/x-raw,width=640,height=480,pixel-aspect-ratio=1/1 \
     ! textoverlay text=Hello font-desc=72 ! xvimagesink \
   videotestsrc ! video/x-raw,width=320,height=480,pixel-aspect-ratio=2/1 \
     ! textoverlay text=Hello font-desc=72 ! xvimagesink \
   videotestsrc ! video/x-raw,width=640,height=240,pixel-aspect-ratio=1/2 \
     ! textoverlay text=Hello font-desc=72 ! xvimagesink
```
