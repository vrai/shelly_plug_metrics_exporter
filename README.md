# Shelly Plug Metrics Exporter

Simple daemon that scrapes local Shelly plugs (for example, the
[Shelly Plus Plug](https://shellystore.co.uk/product/shelly-plus-plug-uk/)) for
energy use metrics and makes them available for scraping in a Prometheus
compatible format.

Note that this project is not affiliated with nor endorsed by
[Shelly Group](https://corporate.shelly.com).

## Overview

The exporter acts as an aggregating scraper for the Shelly plug metrics.
Periodically retrieving the energy use data from each Shelly plug target in
its configuration file using the local REST API that Shelly devices provide,
and making them available via HTTP in a Prometheus compatible format.

TODO(vrai) Diagram

By default the Docker instance of the exporter will serve the metrics on port
9101 rather than the standard 9100 for Prometheus metrics. This is to allow the
host machine to run its own
[node exporter](https://github.com/prometheus/node_exporter) on the default
port.

## Exported metrics

Prometheus metrics are exported via HTTP on port 9101 by default for the Docker
hosted version, and port 9100 by default for the locally hosted version, under
the `/metrics` path (e.g. `http://my.server.lan:9101/metrics`).

Both the port and the serving path can be altered via flags (see the
[Supported flags](#supported-flags) section below).

The exported metrics can be split into two sets: server wide and per-target.

### Server wide metrics

Server wide metrics contain information about the exporter itself, rather than
any of the target Shelly plugs. The exported metrics are:

| Metric name | Type | Description |
| --- | --- | --- |
| `exposer_transferred_bytes_total` | Integer | The total number of bytes transferred by the metrics service. |
| `exposer_scrapes_total` | Integer | The number of calls made to the metrics service.<br />Note that this is not the number of calls made to the targets. |
| `exposer_request_latencies` | Distribution | Distribution of latencies serving metrics requests, in microseconds. |

### Per-target metrics

Per-target metrics have a different value per `target` label. Where the
`target` label contains the name of that particular Shelly plug as defined
in the [configuration file](#configuration-file-format). For example, the
`shelly_success_counter` values for the "My Shelly Plug" target would be
output as:

```json
shelly_success_counter{target="My Shelly Plug"} 123
```

This applies to all per-target metrics, which are:

| Metric name | Type | Description |
| --- | --- | --- |
| `shelly_success_counter` | Integer | The number of successful API calls made to the target. |
| `shelly_error_counter` | Integer | The number of failed API calls made to the target. |
| `shelly_voltage` | Float | The last measured voltage of the target (for plugs, the mains voltage) in volts. |
| `shelly_current` | Float | The last measured current of the target, in amps. |
| `shelly_apower` | Float | The last measured power used by the target, in watts. |
| `shelly_temp_c` | Float | The last measured temperature of the target, in degrees celsius. |
| `shelly_temp_f` | Float | The last measured temperature of the target, in fahrenheit. |
| `shelly_last_updated` | Integer | The timestamp for the last successful API call to the target, in seconds since the Unix epoch. |

## Configuration file format

The target configuration file is simply a JSON map. With the target name
(which the target's metrics will be referenced by) as the key, and the target
host/port as the value.

The host/port value should be in standard URL format of host and port separated by a
colon. Where the host can be either the IP address or the hostname. For example:
  - `192.168.1.1:80`
  - `my.target.lan:8080`

By default this configuration file should be called `targets.json` (this can be
changed using a [flag](#supported-flags)). An example configuration would be:

```json
{
  "Window Plug": "192.168.1.100:80",
  "Wall Plug": "192.168.1.101:80"
}
```

Note that as this is JSON, the last entry in the map cannot have a trailing comma.

## Supported flags

The `shelly_plug_metrics_exporter` binary supports the following flags:

| Flag | Default value | Description |
| --- | --- | --- |
| `metrics_addr` | `0.0.0.0:9100` | Address on which the metrics will be served. Defaults to the standard Prometheus node exporter port. Note that `0.0.0.0` makes it available on all network interfaces. |
| `metrics_path` | `/metrics` | The path (URL suffix) on which the metrics will be served. |
| `poll_period` | `15s` | How frequently the targets will be polled for updated metrics. |
| `targets_config_file` | `./targets.json` | File name of the JSON targets config file. |
| `verbose_scraper` | `false` | If true, log verbose scraper output. |
| `verbose_poller` | `false` | If true, log verbose poller output. |

## Building and running with Docker Compose

The `compose.yml` file containers the Docker Compose rules for building and
running the exporter as a Docker container.

By default the Prometheus metrics will be exported on port 9101 (to allow for
the host to export its own metrics on the default Prometheus port) and be
available on the `/metrics` path.

A [configuration file](#configuration-file-format) must be provided on the
host. By default this should be at
`/etc/shelly_plug_metrics_exports/targets.json`.

With the configuration file in place, the container can be built and started
by running the following from the same directory as `compose.yml`:

```shell
$ docker compose up
```

Note that depending on your Docker installation, it may necessary to run this
command as root or via `sudo`.

You should then be able to view the exported metrics by visiting
'`http://${HOSTNAME}:9101/metrics`. Where `${HOSTNAME}` should be replaced by
the hostname of the machine running the container.

If this is successful you can terminate that container and restart it as a
daemon running in the background using:

```shell
$ docker compose up -d
```

Again this may need to be run as root or using `sudo`.

By default the Docker container will start on boot and automatically restart
if it terminates without being shutdown via Docker.