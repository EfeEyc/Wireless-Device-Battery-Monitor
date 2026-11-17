// VGN / VXE / ATK Evision-based wireless keyboards (VID 320F: 2.4 GHz dongle 5088, USB cable 5055),
// including VXE V87, ATK V75X, VGN VXE75 / V98 Pro / S99.
// Vendor collection FF1C:0092 carries 64-byte output/input report 04:
//   request [04][seq lo][seq hi][1A][0...]
//   reply   [04][seq lo][seq hi][1A][00][00][00][00][percent][state]...
//     percent = 0..100, state = 0 (discharging) or 1..2 (charging)
//   reply   [04][seq lo][seq hi][FF]... -> receiver answered, keyboard is asleep / unreachable
// Protocol from ATK HUB (Evision keyboard controller) and github.com/HeyOkay/HaloBattery (atk_keyboard.py).
#include "drivers.h"

#include <cwctype>

namespace {

enum class Reply { Ok, Asleep, None };

bool IsSupportedName(const std::wstring& product) {
    std::wstring low = product;
    for (auto& ch : low) ch = (wchar_t)towlower(ch);
    return low.find(L"vxe") != std::wstring::npos || low.find(L"vgn") != std::wstring::npos ||
           low.find(L"atk") != std::wstring::npos;
}

std::wstring CleanName(std::wstring n) {
    for (const wchar_t* suffix : { L" 2.4G Dongle", L" Wireless Receiver", L" Receiver", L" Dongle", L" 2.4G" }) {
        size_t l = wcslen(suffix);
        if (n.size() > l && n.compare(n.size() - l, l, suffix) == 0) {
            n.resize(n.size() - l);
            break;
        }
    }
    return n.empty() ? L"VXE wireless keyboard" : n;
}

Reply QueryBattery(HidDev& d, std::vector<BYTE>& r) {
    static WORD g_seq = 0;
    WORD seq = ++g_seq;
    if (seq == 0) seq = ++g_seq;
    std::vector<BYTE> req(32, 0);
    req[0] = 0x04;
    req[1] = (BYTE)(seq & 0xFF);
    req[2] = (BYTE)(seq >> 8);
    req[3] = 0x1A;
    d.Flush();
    if (!d.Write(req)) return Reply::None;
    ULONGLONG end = GetTickCount64() + 1000;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return Reply::None;
        int n = d.Read(r, left);
        if (n < 0) return Reply::None;
        if (n < 10) continue;
        if (r[0] != 0x04 || r[1] != (BYTE)(seq & 0xFF) || r[2] != (BYTE)(seq >> 8)) continue;
        if (r[3] == 0xFF) return Reply::Asleep;
        if (r[3] == 0x1A && r[5] == 0 && r[6] == 0 && r[8] <= 100 && r[9] <= 2) return Reply::Ok;
    }
}

}  // namespace

void PollEvision(PollContext& ctx) {
    const USHORT kVid = 0x320F;
    std::vector<std::wstring> seenKeys;
    // Check the USB cable (5055) before the 2.4 GHz dongle (5088) so the cable wins when both are connected.
    for (USHORT pid : { (USHORT)0x5055, (USHORT)0x5088 }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF1C && c.usage() == 0x0092 && c.outLen() >= 32 && c.inLen() >= 10;
             })) {
            std::wstring product = HidProduct(*c);
            if (!IsSupportedName(product)) continue;
            std::wstring name = CleanName(product);
            std::wstring key = L"evision:" + name;
            bool dup = false;
            for (const auto& k : seenKeys) {
                if (k == key) { dup = true; break; }
            }
            if (dup) {
                ctx.Claim(kVid, pid);
                break;
            }
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> r;
            Reply res = QueryBattery(d, r);
            if (res == Reply::None) continue;
            DeviceStatus s;
            s.key = key;
            s.name = name;
            s.kind = DeviceKind::Keyboard;
            s.via = pid == 0x5055 ? L"USB cable" : L"2.4 GHz receiver";
            if (res == Reply::Ok) {
                s.percent = r[8];
                s.charging = (r[9] == 1 || r[9] == 2);
            } else {
                s.asleep = true;
            }
            seenKeys.push_back(key);
            ctx.Add(s, kVid, pid);
            break;
        }
    }
}
