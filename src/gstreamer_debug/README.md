本プログラムはGStreamerストリームのデバックプログラムである．

# 0. 仮想環境の構築

本プログラムは`Anaconda`で作成した`python3.11`環境で動作確認を行った．実行は同様の環境を用意することを推奨する．

# 1. 依存関係のインストール

画像ストリームと画面表示のためにGStreamerとGTK3を使用する．

## GTK3をインストール

```shell
conda install -c conda-forge gtk3
```

## GStreamerをインストール

```shell
conda install -c conda-forge \
  gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly \
  gst-libav gst-python pygobject
```

# 2. 実行

```shell
python rtp_jpeg_viewer.py
```