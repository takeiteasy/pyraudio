from esc2esc import wait2escape
import pal

pal.initialize()

while wait2escape():
    print(pal.is_ready())

pal.shutdown()
