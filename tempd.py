#!/usr/bin/python3

import asyncio
import subprocess
import sys
import statistics
import math
import functools
import re

import numpy as np


def linear_lubricant(vals):
    weights = range(1, len(vals)+1)
    norm = sum(weights)
    weights = [i/norm for i in weights]
    weighted = [val*weight for val, weight in zip(vals, weights)]
    return statistics.mean(weighted)*len(vals)


READ_ERROR_RE = re.compile(r"read failed \(reason=0x([0-9a-fA-F]+)\)")


class Tempd:
    def __init__(self, loop, sensors, default_sensor):
        self.raw_history = {}
        self.loop = loop
        self.sensors = sensors
        self.default_sensor = default_sensor

        #self.output_history_size = 12
        self.output_history_size = 2 # flow smoothing is disabled
        self.output_history = dict()
        self.output_window_size = 3
        self.stats = dict()

        self.sensor_resolution = 1/16

    def reset_stats(self, sensor):
        self.stats[sensor] = {
            "filtered": 0,
            "accepted": 0
        }

    async def start_child(self):
        self.child = await asyncio.create_subprocess_exec(
            "/home/leon/tempd/test",
            stdout=subprocess.PIPE,
            loop=self.loop
        )

    async def start_server(self):
        self.server = await asyncio.start_server(
            self.handle_connect,
            host="127.0.0.1",
            port=31338,
            loop=self.loop,
        )

    def sensor_name(self, sensor):
        if sensor in self.sensors:
            return self.sensors[sensor]
        else:
            return self.default_sensor

    def get_output(self, sensor):
        median = statistics.median(self.raw_history[sensor])
        #δ = 4 * self.sensor_resolution
        #vals = list(filter(
        #    functools.partial(math.isclose, median, abs_tol=δ),
        #    self.raw_history[sensor]
        #))
        vals = self.raw_history[sensor]

        print("considering for output:", vals, file=sys.stderr, flush=True)

        return statistics.mean(vals)

    def write_output_history(self, sensor, median):
        if sensor not in self.output_history:
            self.output_history[sensor] = []

        self.output_history[sensor].append(median)

        if len(self.output_history[sensor]) > self.output_history_size:
            self.output_history[sensor].pop(0)

    def get_cur_flow(self, sensor):
        pad = self.output_history_size - len(self.output_history[sensor]) - 1
        diff = [0] * pad + list(np.diff(self.output_history[sensor]))

        return linear_lubricant(diff)/5*60

    def get_cur_ratio(self, sensor):
        return 100 * self.stats[sensor]["filtered"] / (
            self.stats[sensor]["filtered"] +
            self.stats[sensor]["accepted"]
        )

    def handle_connect(self, client_reader, client_writer):
        print("incoming connection… ", end="", file=sys.stderr, flush=True)

        output = dict()
        output_flow = dict()

        for sensor in self.raw_history:
            try:
                out = self.get_output(sensor)
                self.write_output_history(sensor, out)
            except statistics.StatisticsError:
                out = "NaN"

            try:
                flow = self.get_cur_flow(sensor)
            except (KeyError, statistics.StatisticsError):
                flow = "NaN"

            try:
                ratio = self.get_cur_ratio(sensor)
            except (KeyError, ZeroDivisionError):
                ratio = "NaN"

            msg = "multigraph sensors_{}\n".format(sensor)
            msg += "{}.value {}\n".format(sensor, out)
            msg += "multigraph sensors_{}_flow\n".format(sensor)
            msg += "{}-flow.value {}\n".format(sensor, flow)
            msg += "multigraph sensors_{}_stats\n".format(sensor)
            msg += "{}-ratio.value {}\n".format(sensor, ratio)

            print("sending {}".format(msg), file=sys.stderr, flush=True)
            client_writer.write(msg.encode("utf-8"))

            output[sensor] = out
            output_flow[sensor] = flow

            self.raw_history[sensor] = []
            self.reset_stats(sensor)

        msg = "multigraph sensors\n"
        for sensor in output:
            msg += "{}.value {}\n".format(sensor, output[sensor])
        msg += "multigraph sensors_flow\n"
        for sensor in output_flow:
            msg += "{}-flow.value {}\n".format(sensor, output_flow[sensor])

        print("sending {}".format(msg), file=sys.stderr, flush=True)
        client_writer.write(msg.encode("utf-8"))

        client_writer.close()
        print("raw_history:", self.raw_history, file=sys.stderr, flush=True)
        print("output_history:", self.output_history, file=sys.stderr, flush=True)

    async def run(self, loop):
        await asyncio.wait([self.start_child(), self.start_server()])
        while self.child.returncode is None:
            line = await self.child.stdout.readline()
            if not line:
                print("child process has died, exiting…", file=sys.stderr)
                return

            line = line.decode().strip()
            addr, msg = line.split(" ", 1)

            sensor = self.sensor_name(addr)

            if sensor not in self.stats:
                self.reset_stats(sensor)

            try:
                val = float(msg)
            except ValueError:
                # this is an error from test.c
                print(msg)
                m = READ_ERROR_RE.match(msg)
                if m:
                    reason = int(m.group(1), 16)
                    if reason == 0x04:  # crc error
                        self.stats[sensor]["filtered"] += 1
                continue

            if val > 84:
                print("Skipping bad value:", val,
                      file=sys.stderr, flush=True)
                self.stats[sensor]["filtered"] += 1
                continue


            self.stats[sensor]["accepted"] += 1

            if sensor not in self.raw_history:
                self.raw_history[sensor] = []

            self.raw_history[sensor].append(val)
            print(self.raw_history, file=sys.stderr, flush=True)


if __name__ == "__main__":
    sensors = {
        "2846b25204000054": "wohnzimmer",
        "285edd52040000d0": "wohnzimmer-heizung"
    }
    loop = asyncio.get_event_loop()
    tempd = Tempd(loop, sensors, default_sensor="wohnzimmer")
    loop.run_until_complete(tempd.run(loop))
