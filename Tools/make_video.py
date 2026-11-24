"""Development 패키지의 -TwinShowcase로 소개 영상을 만든다. 에뮬레이터는 꺼 둔다. 약 12분.

    python Tools/make_video.py        (opencv-python 필요)
"""
import glob
import os
import shutil
import socket
import subprocess
import sys

import cv2

FPS, EXPECTED = 30, 3000
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = os.path.join(ROOT, "Build", "Package", "Development", "Windows")
FRAMES = os.path.join(PKG, "MetaTwin", "Saved", "Showcase")
OUT = os.path.join(ROOT, "Saved", "MetaTwin_master.mp4")

with socket.socket() as s:
    if s.connect_ex(("127.0.0.1", 8765)) == 0:
        sys.exit("8765 포트를 쓰는 에뮬레이터를 먼저 끄세요")
shutil.rmtree(FRAMES, ignore_errors=True)
subprocess.run([os.path.join(PKG, "MetaTwin.exe"), "-TwinShowcase", "-benchmark", f"-fps={FPS}", "-RenderOffscreen", "-windowed",
                "-ResX=1920", "-ResY=1080", "-ForceRes", "-nosound", "-log=Showcase.log"], timeout=1800)
files = sorted(glob.glob(os.path.join(FRAMES, "*.png")))
print("frames", len(files))
if len(files) < EXPECTED - 5:
    sys.exit("프레임이 부족합니다")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
size = cv2.imread(files[0]).shape[1::-1]
# Windows Media Foundation의 H.264를 먼저 쓰고, 안 되면 mp4v로 저장한다.
for api, codec in ((cv2.CAP_MSMF, "H264"), (cv2.CAP_FFMPEG, "mp4v")):
    writer = cv2.VideoWriter(OUT, api, cv2.VideoWriter_fourcc(*codec), FPS, size)
    if writer.isOpened():
        break
else:
    sys.exit("영상 인코더를 열 수 없습니다")
for f in files:
    writer.write(cv2.imread(f))
writer.release()
check = cv2.VideoCapture(OUT)
print(codec, size, "frames in video", int(check.get(cv2.CAP_PROP_FRAME_COUNT)), f"{os.path.getsize(OUT) / 1e6:.1f} MB")
