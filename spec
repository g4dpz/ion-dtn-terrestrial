Design outline: terrestrial ION-DTN demo over UHF amateur packet using AX.25 / KISS

This is a good fit for ION-DTN because it lets you show the two behaviors that matter most:

point-to-point delayed delivery

relay / store-and-forward delivery across multiple hops

The simplest credible architecture is to use AX.25 packet over UHF for the RF hop, with the TNC exposed in KISS mode, and run ION-DTN on small Linux hosts attached to each radio.

1. Demonstration goals

The demo should prove four things:

ION can send and receive bundles over an amateur packet link

bundles can be stored when the RF path is unavailable

a relay node can receive, retain, and later forward bundles

the application does not need continuous end-to-end connectivity

That gives you both a terrestrial networking demo and a clear analogue for later satellite and cislunar work.

2. High-level architecture

A practical 3-node layout is:

Application A
   |
ION Node A
   |
AX.25 / KISS TNC
   |
UHF packet link
   |
ION Node B (relay)
   |
AX.25 / KISS TNC
   |
UHF packet link
   |
ION Node C
   |
Application C

Where:

Node A = originator

Node B = relay / store-and-forward node

Node C = destination

You can also run a simpler 2-node point-to-point version first:

App A -> ION A -> AX.25/KISS/UHF -> ION B -> App B

Then add the relay node.

3. Recommended system concept
Node functions
Node A

runs ION

accepts data from a local demo app

creates outbound bundles

forwards them over the UHF packet link when possible

Node B

runs ION

acts as an intermediate DTN forwarder

stores received bundles persistently

forwards them when the next link becomes available

Node C

runs ION

receives final bundles

delivers payloads to the local app

4. Radio and protocol stack
RF layer

UHF amateur packet

simplex is easiest for first tests

half-duplex operation is acceptable and actually useful for the demo

Link layer

AX.25

preferably unproto or connected mode only if required by your chosen transport approach

TNC interface

KISS mode

exposed to the host over serial or USB-serial

Above the TNC

You have two realistic integration models.

Option A: AX.25 carries IP, and ION runs over IP

This is the easiest path.

Stack:

ION / Bundle Protocol
   ->
UDP or TCP convergence layer
   ->
IP
   ->
AX.25
   ->
KISS TNC
   ->
UHF RF

This is the best choice for a first demo because:

least custom code

easier debugging

cleaner separation between DTN and radio

lets you focus on DTN behavior rather than transport development

Option B: custom serial / KISS transport below ION

Stack:

ION / Bundle Protocol
   ->
custom CLA-like shim
   ->
KISS frames
   ->
AX.25
   ->
UHF RF

This is architecturally interesting, but more work. It makes sense later if you want a more flight-like convergence-layer adaptor.

For the first terrestrial demo, use Option A.

5. Recommended implementation approach
Baseline recommendation

Use:

small Linux PCs or Raspberry Pi-class hosts

one UHF packet radio + one TNC per RF-facing node

TNCs in KISS mode

Linux AX.25 stack or IP-over-AX.25 support

ION on each node

Why this works well

It lets you:

move bundles over real RF

deliberately drop links

schedule availability windows

inspect queues at each node

show relay behavior clearly

6. Suggested physical configurations
Configuration 1: bench demo

Good for initial bring-up.

all three nodes in one room

radios into dummy loads or very low power with attenuation

controlled interference-free environment

Use this to validate:

TNC operation

KISS framing

AX.25 addressing

IP connectivity

ION routing and forwarding

Configuration 2: short-range field demo

Good for public demonstrations.

Node A and Node B separated geographically

Node B and Node C on another link or at another site

real over-the-air packet paths

Use this to validate:

true disrupted operations

scheduled forwarding

relay persistence

7. Functional modes to demonstrate
Mode 1: point-to-point real-time when link is up

With Nodes A and C or A and B directly linked:

send a short message

show immediate transfer

This proves the basic stack works.

Mode 2: delayed point-to-point

disable the RF link

inject a message at Node A

show bundle queued in ION

restore the link

show eventual delivery

This is the first true DTN demonstration.

Mode 3: relay / store-and-forward

A sends to C via B

B receives and stores

B cannot reach C immediately

later the B-C link is enabled

B forwards to C

This is the most important mission-like demonstration.

Mode 4: interrupted forwarding

begin forwarding

break the RF link mid-transfer

show retained state / reattempt later

complete delivery on a later contact

8. Contact-plan concept

To make it more DTN-like, operate links as scheduled contacts rather than permanent availability.

Example:

A <-> B available from minute 0 to 5

unavailable from minute 5 to 20

B <-> C available from minute 10 to 15

This creates a natural store-and-forward case:

A sends to B during first window

B stores

B forwards to C during later window

That is an excellent analogue of satellite-pass operations.

9. Logical addressing concept

You need two addressing layers.

Amateur radio / AX.25 addresses

Each RF node needs:

amateur callsign

SSID as needed

Example:

A = G0AAA-1

B = G0BBB-1

C = G0CCC-1

DTN endpoint IDs

Each ION node needs its own BP node identity.

Example:

Node A = ipn:10.x

Node B = ipn:20.x

Node C = ipn:30.x

Use simple, fixed numbering for the demo.

10. ION role mapping

A simple mapping could be:

Node A: source application + BP sender

Node B: forwarder + persistent store

Node C: sink application + BP receiver

Applications can be very simple:

text-message sender

file sender

telemetry object sender

receive-and-log utility

For the public demo, short timestamped text messages work very well.

11. Suggested data products for the demo

Use small payloads first:

timestamped text messages

short JSON telemetry objects

tiny image thumbnails

small log files

Good example payload:

{
  "origin": "NodeA",
  "created": "2026-03-12T14:00:00Z",
  "type": "status",
  "value": "RF test message 17"
}

That makes it easy to show:

creation time

delayed arrival time

successful relay path

12. Relay-node store-and-forward behavior

The relay node should explicitly demonstrate:

reception from upstream node

local persistence

queued state while next hop unavailable

forwarding on next contact

queue depth decreasing after transmission

This is what makes the demo compelling. A simple operator display on Node B is worth having:

bundles received

bundles queued

next hop status

bundles forwarded

13. Transport options between ION and radio
Preferred

ION over UDP/TCP over IP-over-AX.25

Advantages:

easiest to implement

easiest to troubleshoot with standard tools

avoids writing a custom CLA initially

Later enhancement

ION over a custom AX.25/KISS convergence adapter

Advantages:

more compact

more native use of packet

closer to a purpose-built constrained-link integration

But it is not necessary for the first demonstration.

14. Hardware outline

A realistic node build is:

Linux host computer

USB-connected TNC in KISS mode

UHF transceiver

antenna or attenuated bench path

local storage for ION bundle persistence

optional GPS or RTC for accurate timestamps

For the relay node:

either one radio shared across time-separated links

or two radios / TNCs if you want simultaneous independent A-B and B-C segments

For simplicity, you can also emulate one leg at a time in early testing.

15. Software outline

Each node needs:

Linux OS

AX.25 utilities and support

KISS-capable TNC connection

IP-over-AX.25 or chosen link integration

ION-DTN installed and configured

simple send/receive test apps

basic monitoring scripts

Useful local tools:

serial/TNC status checks

AX.25 interface status

routing inspection

ION queue / bundle inspection

logging and timestamps

16. Stepwise build plan
Phase 1: bench link bring-up

configure two TNCs in KISS

verify AX.25 packet exchange

verify IP over the radio path

validate stability at low data rates

Phase 2: 2-node ION test

install ION on both hosts

establish BP connectivity over the packet link

send small bundles end-to-end

Phase 3: delayed-delivery test

send while link is down

show bundle queued

restore link

show delivery

Phase 4: 3-node relay test

add Node B

route A to C via B

enable only A-B first

store at B

enable B-C later

deliver to C

Phase 5: scheduled-contact demo

automate radio/interface enable/disable by schedule

demonstrate repeated store-carry-forward behavior

17. Public demonstration script

A clean demo sequence:

Show all three nodes and their callsigns

Show that A-B is up, B-C is down

Send message from A to C

Show arrival at B only

Show it queued on B

Later enable B-C

Show B forwarding

Show delivery at C with original and received timestamps

That communicates the concept immediately.

18. Key engineering choices
Choice 1: connected AX.25 vs unproto-style carriage

For a first demo, use whichever mode is easiest to make stable with your chosen TNC and Linux stack. The main goal is reliable transfer of BP traffic, not purity.

Choice 2: one relay radio or two

one radio: simpler hardware, slower, more sequential operations

two radios: better separation of upstream/downstream links, cleaner relay demo

Choice 3: static contacts vs dynamic links

Start with static scheduled contacts. They are easier to explain and verify.

19. Risks and mitigations
Low throughput

Keep bundles small and transmissions short.

Fragmentation overhead

Avoid large payloads initially.

TNC quirks

Standardize on one known-good KISS TNC type for all nodes.

Link instability

Begin on bench with attenuation, then move outdoors.

Too much custom software

Do not start with a custom CLA. Use IP-over-AX.25 first.

20. Success criteria
Minimum success

one bundle transferred point-to-point over UHF packet

Good success

a bundle queued during outage and delivered later

Strong success

A-to-C delivery through B with B performing store-and-forward across separate contacts

Excellent success

repeated automated contact-plan operation over multiple cycles

21. Best first configuration

The most practical starting point is:

3 Linux nodes

3 KISS TNCs

3 UHF packet radios

AX.25 with IP over radio

ION on each node

Node B as the relay

Then implement:

direct A-B link

direct B-C link

scheduled availability

small text or JSON bundle transfers

That gives you a clean demonstration of:

point-to-point DTN

relay DTN

store-and-forward

intermittent-contact operations

Concise system summary

Use ION-DTN on Linux hosts attached to UHF amateur packet radios through KISS-mode TNCs, carry IP over AX.25 for the first implementation, and build a 3-node topology where the middle node stores bundles received from one contact and forwards them during a later contact to demonstrate terrestrial DTN relay behavior.

I can turn this into a more concrete node-by-node build sheet next, with suggested interfaces, example topology, and what runs on Nodes A, B, and C.


## AMPRNet IP Addressing

For proper amateur radio operation, nodes should use IP addresses from the AMPRNet (Amateur Packet Radio Network) address space (44.0.0.0/8).

**Obtaining AMPRNet Addresses:**
- Register at: https://portal.ampr.org/
- Request IP allocation for your callsign
- Use allocated 44.x.y.z addresses instead of 192.168.x.x for production deployment
- AMPRNet addresses are globally routable within the amateur radio network

**Example Allocation:**
- Node A (G4DPZ-1): 44.x.y.1
- Node B (G4DPZ-2): 44.x.y.2
- Node C (G4DPZ-3): 44.x.y.3

Where x.y is your allocated subnet from AMPR.

**Note:** For initial testing and development, private IP addresses (192.168.25.x) can be used, but production deployments should use AMPRNet addresses.
