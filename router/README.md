# Router

A router working on mininet that supports ARP, IP, ICMP protocols, achieving `ping`, `traceroute`, `curl` between hosts.

## How to run this

Set up the container based on `.devcontainer`.

Compile:

```bash
make
```

Set up the environment:

First load `openvswitch`:

```bash
modprobe openvswitch
```

Then run POX and mininet:

```bash
cd /project-base
./run_pox.sh
```

```bash
cd /project-base
./run_mininet.sh
```

Spin our router:

```bash
./sr
```

Do stuffs in mininet:

```bash
> client ping server1
```
