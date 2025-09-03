import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib
import argparse, sys
import numpy as np
import cv2

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5540, help="UDP port for RTP/JPEG (default: 5540)")
    parser.add_argument("--latency", type=int, default=0, help="rtpjitterbuffer latency ms (0 for ultra low)")
    args = parser.parse_args()

    Gst.init(None)

    # ESP32S3 → RTP/JPEG (PT=26, clock-rate=90000) を想定
    pipe = f'''
        udpsrc port={args.port} caps="application/x-rtp,media=video,encoding-name=JPEG,payload=26,clock-rate=90000"
        ! rtpjitterbuffer latency={args.latency} drop-on-latency=true
        ! rtpjpegdepay
        ! jpegdec
        ! videoconvert
        ! video/x-raw,format=BGR
        ! appsink name=sink emit-signals=true max-buffers=2 drop=true sync=false
    '''

    pipeline = Gst.parse_launch(pipe)
    appsink = pipeline.get_by_name("sink")

    # new-sample コールバックで1フレームずつOpenCV表示
    def on_new_sample(sink):
        sample = sink.emit("pull-sample")
        buf = sample.get_buffer()
        caps = sample.get_caps()
        s = caps.get_structure(0)
        w = s.get_value("width")
        h = s.get_value("height")

        ok, mapinfo = buf.map(Gst.MapFlags.READ)
        if not ok:
            return Gst.FlowReturn.ERROR

        # BGR 3ch フレームとして NumPy に載せ替え
        frame = np.frombuffer(mapinfo.data, dtype=np.uint8)
        frame = frame.reshape((h, w, 3))
        buf.unmap(mapinfo)

        cv2.imshow("RTP/JPEG (ESP32S3)", frame)
        # ESC で終了
        if cv2.waitKey(1) & 0xFF == 27:
            pipeline.set_state(Gst.State.NULL)
            
            loop.quit()
        return Gst.FlowReturn.OK

    appsink.connect("new-sample", on_new_sample)

    # 実行
    pipeline.set_state(Gst.State.PLAYING)
    print(f"[INFO] Listening RTP/JPEG on UDP {args.port} ... (ESC to quit)")
    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    finally:
        pipeline.set_state(Gst.State.NULL)
        cv2.destroyAllWindows()

if __name__ == "__main__":
    sys.exit(main())


