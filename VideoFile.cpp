#include "VideoFile.h"
#include "WatchThatCode.h"
#include <QDebug>
#include <QFileInfo>

using namespace WTS;

bool VideoFile::s_ffInited = false;

struct LibAvLogger {
    bool operator<<(int av_err) {
        if (av_err) {
            char buff[1024];
            QString msg;

            if (av_strerror(av_err, buff, 1024) == 0)
                qDebug() << (msg = buff);
            else
                qDebug() << (msg = "Unknown problem with ffmpeg libraries");

            TRY_ASSERT_X(!av_err,msg);
        }
        return true;
    }
};

VideoFile::VideoFile(QObject *parent)
    : QObject(parent)
    , m_formatContext(0)
    , m_codecContext(0)
    , m_codec(0)
    , m_frame(0)
    , m_frameRGB(0)
    , m_convertContext(0)
{

}
/*
 * open file, create format context, codec context, etc -
 * everything to be able to seek / decode
 */
VideoFile::VideoFile(QString path, QObject *parent)
    : QObject(parent)
    , m_formatContext(0)
    , m_codecContext(0)
    , m_codec(0)
    , m_frame(0)
    , m_frameRGB(0)
    , m_convertContext(0)
{
    open(path);
}

void VideoFile::open(QString path)
{
    if (!s_ffInited) {
        av_register_all();
        s_ffInited = true;
    }

    QFileInfo fi(path);
    path = fi.canonicalFilePath();

    LibAvLogger() << av_open_input_file( &m_formatContext,
        qPrintable(path), NULL, 0, NULL);

    // HACK this solves "max_analyze_duration reached" warning
    m_formatContext->max_analyze_duration *= 10;

    av_find_stream_info(m_formatContext);
    av_dump_format(m_formatContext, 0, qPrintable(path), 0);

    // Find the first video stream
    m_streamIndex=-1;
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if ( m_formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO ) {
            m_streamIndex=(int)i;
            break;
        }
    }
    TRY_ASSERT_X ( m_streamIndex != -1, "No video streams found");

    m_codecContext = m_formatContext->streams[m_streamIndex]->codec;
    m_codec = avcodec_find_decoder(m_codecContext->codec_id);


    TRY_ASSERT_X (m_codec, QString("Missing codec: %1")
            .arg(m_formatContext->streams[m_streamIndex]->codec->codec_name));

    LibAvLogger() << avcodec_open(m_codecContext, m_codec);

    /*
     * Hack to correct wrong frame rates that seem to be generated by some codecs
     * cf.: http://web.me.com/dhoerl/Home/Tech_Blog/Entries/2009/1/
     *                                              22_Revised_avcodec_sample.c.html
     */
    if (m_codecContext->time_base.num > 1000
        && m_codecContext->time_base.den == 1)
        m_codecContext->time_base.den=1000;

    m_frame = avcodec_alloc_frame();
    m_frameRGB = avcodec_alloc_frame();
    int bufSize = avpicture_get_size(PIX_FMT_RGB24,
                                     m_codecContext->width,
                                     m_codecContext->height);

    m_frameBytes = QByteArray(bufSize, 0);
    avpicture_fill((AVPicture *)m_frameRGB,
                   (uint8_t*) m_frameBytes.data(),
                   PIX_FMT_RGB24,
                   m_codecContext->width,
                   m_codecContext->height);

    int w = m_codecContext->width, h = m_codecContext->height;

    // FIXME reimplement using undeprecated API
    // I can't understand what would be the equivalent to this
    m_convertContext = sws_getContext(w, h, m_codecContext->pix_fmt,
                                      w, h, PIX_FMT_RGB24,
                                      SWS_BICUBIC, 0, 0, 0);

    TRY_ASSERT(m_convertContext);

}

VideoFile::~VideoFile()
{
    if (m_convertContext)
        sws_freeContext(m_convertContext);
    if (m_frameRGB)
        av_free(m_frameRGB);
    if (m_frame)
        av_free(m_frame);
    if (m_codecContext)
        avcodec_close(m_codecContext);
    if (m_formatContext)
        av_close_input_file(m_formatContext);
}

QImage VideoFile::frame()
{
    QImage image;

    AVPacket packet;
    int done = 0;
    while (av_read_frame(m_formatContext, &packet) >= 0) {
        if (packet.stream_index == m_streamIndex) {
            avcodec_decode_video2(
                        m_codecContext,
                        m_frame,
                        &done,
                        &packet);
            if (done) {
                sws_scale(m_convertContext,
                          m_frame->data, m_frame->linesize, 0,
                          m_codecContext->height,
                          m_frameRGB->data, m_frameRGB->linesize);
                // convert to QImage and return
                image = QImage((uchar*)m_frameBytes.data(),
                               m_codecContext->width,
                               m_codecContext->height,
                               QImage::Format_RGB888);
                break;
            }
        }
    }

    return image;
}

bool VideoFile::nextPacket(AVPacket& packet)
{
    while (av_read_frame(m_formatContext, &packet) >= 0) {
        if (packet.stream_index == m_streamIndex) {
            return true;
        }
    }
    // end of stream reached
    return false;
}

int VideoFile::width() const
{
    return m_codecContext->width;
}
int VideoFile::height() const
{
    return m_codecContext->height;
}

void VideoFile::seek(qint64 ms)
{
    // ms to time_base units...
    AVRational sec = { (int)ms, 1000};
    AVRational ts = av_div_q(sec,
                             m_formatContext->streams[m_streamIndex]->time_base);
    av_seek_frame(m_formatContext, m_streamIndex, ts.num/ts.den, 0);
}

CodecID VideoFile::codecId() const
{
    TRY_ASSERT(m_codec);
    return m_codec->id;
}

const AVCodecContext * VideoFile::codec() const
{
    TRY_ASSERT(m_codecContext);
    return m_codecContext;
}

const AVStream * VideoFile::stream() const
{
    TRY_ASSERT(m_formatContext);
    TRY_ASSERT(m_streamIndex >= 0
             && m_streamIndex < (int)m_formatContext->nb_streams);
    return m_formatContext->streams[m_streamIndex];
}

qint64 VideoFile::duration() const
{
    TRY_ASSERT(m_formatContext);
    TRY_ASSERT(m_streamIndex >= 0
             && m_streamIndex < (int)m_formatContext->nb_streams);

    AVRational q_duration = {
        m_formatContext->streams[m_streamIndex]->duration,
        1 };

    AVRational seconds = av_mul_q(
                q_duration,
                m_formatContext->streams[m_streamIndex]->time_base );

    return 1000 * (qint64)seconds.num / (qint64)seconds.den;

}
