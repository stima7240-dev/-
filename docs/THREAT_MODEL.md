# AeroMesh Threat Model

A privacy tool that overpromises is dangerous: users make risky decisions based
on guarantees that do not hold. This document states plainly what AeroMesh aims
to protect against and what it does **not**. It will be kept honest as the
project evolves.

## What AeroMesh aims to protect

- **Message confidentiality & integrity.** End-to-end encryption (planned
  Double Ratchet) means relays and storage nodes cannot read content. Forward
  secrecy limits the damage of a key compromise.
- **No central point of seizure/blocking.** Peer discovery via DHT + local
  fallbacks means there is no company server to subpoena or IP to block once a
  client is bootstrapped.
- **Metadata minimization on the wire.** Fixed-size frames + cover traffic make
  message size and (with randomized timing) message timing hard to read for a
  passive network observer.
- **No identity-to-phone linkage.** Accounts are keypairs, not phone numbers.

## What AeroMesh does NOT protect against (current honest limits)

- **A compromised endpoint.** If your device is malware-infected or seized
  unlocked, encryption does not help. AeroMesh is not anti-forensics.
- **Global passive adversaries.** A well-resourced observer who can watch a
  large fraction of the network may still perform traffic-correlation attacks.
  Cover traffic raises the cost but does not make this impossible.
- **Bootstrap-time exposure.** Connecting to seed nodes / DNS fallback can
  reveal that you are *using* AeroMesh, even if not what you say. Pluggable
  transports reduce but do not eliminate this.
- **Unaudited cryptography.** Until the protocol has an independent security
  audit, treat all guarantees as provisional. Do not rely on it where failure
  means physical danger.
- **The DHT is partially observable.** Participating in a DHT inherently leaks
  some routing metadata to peers.

## Responsible-use posture

Censorship-resistant and privacy-preserving software is a legitimate and
important category of open source (Tor, Signal, Briar). To keep the project
defensible and trustworthy:

- Ship the honest threat model with the product; never advertise "absolute
  anonymity."
- Get an independent cryptography/security review before recommending the tool
  to at-risk users.
- **Coin & fiat exchange:** the in-app privacy coin and fiat↔crypto P2P
  exchange are the highest-risk components, legally and from an abuse
  standpoint. An anonymous fiat on-ramp is regulated money transmission in most
  jurisdictions (AML/KYC, licensing). The recommendation is to keep these as
  research modules and obtain legal counsel before any public, fiat-touching
  deployment.
