#!/usr/bin/python3

import asyncio
import subprocess
import sys
import statistics

class Tempd:
    def __init__(self, loop, sensors):
        self.data = {}
        self.loop = loop
        self.sensors = sensors

        self.stats = None
        self.reset_stats()

    def reset_stats(self):
        self.stats = {
            "filtered": 0,
            "accepted": 0
        }

    async def start_child(self):
        self.child = await asyncio.create_subprocess_exec(
            "/home/leon/test",
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            loop=self.loop
        )

    async def start_server(self):
        self.server = await asyncio.start_server(
            self.handle_connect,
            host="127.0.0.1",
            port=31338,
            loop=self.loop,
        )

    def handle_connect(self, client_reader, client_writer):
        print("incoming connection… ", end="", file=sys.stderr, flush=True)
        for sensor in self.data:
            if sensor in self.sensors:
                sensor_name = self.sensors[sensor]
            else:
                sensor_name = sensor

            try:
                val = statistics.median(self.data[sensor])
            except statistics.StatisticsError:
                val = "NaN"

            msg = "multigraph sensors\n"
            msg += "{}.value {}\n".format(sensor_name, val)

            try:
                ratio = self.stats["filtered"]/(
                    self.stats["filtered"] + self.stats["accepted"]
                )*100
            except ZeroDivisionError:
                ratio = "NaN"

            msg += "multigraph sensors_stats\n"
            msg += "{}-ratio.value {}\n".format(sensor_name, ratio)

            print("sending {}".format(msg), file=sys.stderr, flush=True)
            client_writer.write(msg.encode("utf-8"))
            client_writer.close()

            self.data[sensor] = []
            self.reset_stats()

        print(self.data, file=sys.stderr, flush=True)

    async def run(self, loop):
        await asyncio.wait([self.start_child(), self.start_server()])
        while self.child.returncode is None:
            line = await self.child.stdout.readline()
            if not line:
                print("child process has died, exiting…", file=sys.stderr)
                return

            line = line.decode().strip()
            addr, val = line.split(" ")
            val = float(val)

            if val < 0 or val > 80:
                print(
                    "skipping bad value {}".format(val),
                    file=sys.stderr,
                    flush=True
                )
                self.stats["filtered"] += 1
                continue

            self.stats["accepted"] += 1

            if addr not in self.data:
                self.data[addr] = []

            self.data[addr].append(val)
            print(self.data, file=sys.stderr, flush=True)



if __name__ == "__main__":
    sensors = {
        "2846b25204000054": "wohnzimmer"
    }
    loop = asyncio.get_event_loop()
    tempd = Tempd(loop, sensors)
    loop.run_until_complete(tempd.run(loop))
