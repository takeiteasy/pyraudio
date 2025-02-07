from signal import signal, SIGINT
from sys import exit
import pal

def hndl(_, __):
    exit(0)

pal.initialize()
assert(pal.is_ready())
music = pal.Music(path="country.mp3")
assert(music.is_ready())
music.play()

signal(SIGINT, hndl)

print("Press CTRL+C to interrupt...")
while True:
    music.update()

pal.shutdown()
