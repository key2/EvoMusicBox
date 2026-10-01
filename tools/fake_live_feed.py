#!/usr/bin/env python3
"""Generates a synthetic TikTok LIVE event stream as JSON lines (one event per line).

The application itself injects synthetic events through the Simulate buttons / Live menu, which
exercise the exact routing code path. This script is for scripting scenarios outside the app:
pipe its output into your own tooling, or use it as a reference for the event shape used by
docs/architecture.md (LiveEvent fields).

Usage: fake_live_feed.py [--rate 2] [--seconds 30] [--seed 1]
"""
import argparse
import json
import random
import time

GIFTS = [
    (5655, "Rose", 1, 1), (5827, "TikTok", 1, 1), (7934, "Heart Me", 15, 1), (5269, "Doughnut", 30, 1),
    (6671, "Hand Hearts", 100, 1), (7121, "Confetti", 100, 0), (5487, "Galaxy", 1000, 0),
    (5658, "Lion", 29999, 0), (5879, "Money Gun", 500, 1),
]
USERS = ["luna_88", "mike.tv", "dj_nova", "sara_k", "pixel_pat"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=float, default=2.0, help="events per second")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    t_end = time.time() + a.seconds
    print(json.dumps({"type": "Connect", "roomUser": "demo", "viewers": 120}))
    streak = None
    while time.time() < t_end:
        r = rnd.random()
        user = rnd.choice(USERS)
        if streak:
            gid, name, dia, count = streak
            count += 1
            done = rnd.random() < 0.4
            print(json.dumps({"type": "Gift", "user": user, "giftId": gid, "giftName": name, "diamondCount": dia,
                              "repeatCount": count, "streaking": not done, "giftType": 1}))
            streak = None if done else (gid, name, dia, count)
        elif r < 0.35:
            gid, name, dia, typ = rnd.choice(GIFTS)
            if typ == 1:
                streak = (gid, name, dia, 1)
                print(json.dumps({"type": "Gift", "user": user, "giftId": gid, "giftName": name, "diamondCount": dia,
                                  "repeatCount": 1, "streaking": True, "giftType": 1}))
            else:
                print(json.dumps({"type": "Gift", "user": user, "giftId": gid, "giftName": name, "diamondCount": dia,
                                  "repeatCount": 1, "streaking": False, "giftType": 0}))
        elif r < 0.65:
            print(json.dumps({"type": "Like", "user": user, "likeCount": rnd.randint(1, 15)}))
        elif r < 0.80:
            print(json.dumps({"type": "Comment", "user": user, "comment": rnd.choice(["nice!", "play the horn", "hello from Paris", "lol"])}))
        elif r < 0.88:
            print(json.dumps({"type": "Join", "user": user}))
        elif r < 0.94:
            print(json.dumps({"type": "Follow", "user": user}))
        elif r < 0.98:
            print(json.dumps({"type": "Share", "user": user}))
        else:
            print(json.dumps({"type": "Subscribe", "user": user}))
        time.sleep(1.0 / max(0.1, a.rate))
    print(json.dumps({"type": "LiveEnd"}))


if __name__ == "__main__":
    main()
