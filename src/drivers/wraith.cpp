// Wraith W1 / W1 Freestyle wireless mouse (VID 093A: 2.4 GHz dongle 522C, USB cable 622C).
// On vendor collection FF05:0001, the mouse pushes 33-byte input report 0x09:
//   r[0] = 0x09 (report ID)
//   r[1] = (charging ? 0x80 : 0) | (percent & 0x7F)
// Protocol from w1.software (WebHID) and github.com/torchizm/wraith-w1-driver.
#include "drivers.h"

namespace {

bool ReadStatus(HidDev& d, int& percent, bool& charging) {
    // Do not flush: an already-queued report 0x09 is valid.
    ULONGLONG end = GetTickCount64() + 1500;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return false;
        std::vector<BYTE> r;
        int n = d.Read(r, left);
        if (n < 0) return false;
        if (n >= 10 && r[0] == 0x09) {
            int pct = r[1] & 0x7F;
            if (pct <= 100) {
                percent = pct;
                charging = (r[1] & 0x80) != 0;
                return true;
            }
        }
    }
}

}  // namespace

void PollWraith(PollContext& ctx) {
    const USHORT kVid = 0x093A;
    bool added = false;
    // Check USB cable (622C) before 2.4 GHz dongle (522C).
    for (USHORT pid : { (USHORT)0x622C, (USHORT)0x522C }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF05 && c.usage() == 0x0001 && c.inLen() >= 10;
             })) {
            if (added) {
                ctx.Claim(kVid, pid);
                break;
            }
            HidDev d;
            if (!d.Open(*c)) continue;
            DeviceStatus s;
            s.key = L"wraith:w1";
            s.name = L"Wraith W1";
            s.kind = DeviceKind::Mouse;
            s.via = pid == 0x622C ? L"USB cable" : L"2.4 GHz receiver";
            int pct = -1;
            bool chg = false;
            if (ReadStatus(d, pct, chg)) {
                s.percent = pct;
                s.charging = chg;
            } else {
                s.asleep = true;
            }
            ctx.Add(s, kVid, pid);
            added = true;
            break;
        }
    }
}
