from signal import signal, SIGINT
from sys import exit
import raudio

def hndl(_, __):
    exit(0)

raudio.initialize()
assert(raudio.is_ready())
music = raudio.Music(path="country.mp3")
assert(music.is_ready())
music.loop = True
music.play()

signal(SIGINT, hndl)

print("Press CTRL+C to interrupt...")
while True:
    music.update()

raudio.shutdown()
