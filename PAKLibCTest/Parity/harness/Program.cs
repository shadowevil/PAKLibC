// Parity harness: writes what PAKLib (C#, .NET 8) does with each input of the parity corpus.
// Usage: ParityHarness <cases dir> <list file: one file name per line> <report file>
// Files ending in .img are images; everything else is PAK data. The C++ twin is ..\ParityReport.h; the replay
// test (TestsParity.cpp) compares the two line for line, allowing only the hunks listed in ..\deviations.txt.
using System.Text;
using PAKLib;

static class Program
{
    static ulong Fnv(byte[] d)
    {
        ulong h = 14695981039346656037UL;
        foreach (byte b in d) { h ^= b; h *= 1099511628211UL; }
        return h;
    }

    static string Hex(ulong h) => h.ToString("x16");

    static string Err(Exception e) => e.GetType().Name + ": " + e.Message;

    static string Key(uint? k) => k.HasValue ? k.Value.ToString("X8") : "none";

    static string SpriteLine(Sprite s)
    {
        using var ms = new MemoryStream();
        using (var w = new BinaryWriter(ms, Encoding.UTF8, true))
            foreach (var r in s.Rectangles) { w.Write(r.x); w.Write(r.y); w.Write(r.width); w.Write(r.height); w.Write(r.pivotX); w.Write(r.pivotY); }
        return $"r={s.Rectangles.Count} rh={Hex(Fnv(ms.ToArray()))} k={Key(s.ColorKey)} len={s.data.Length} h={Hex(Fnv(s.data))}";
    }

    static string Pixels(byte[] data)
    {
        try { var im = SpriteImage.Decode(data); return $"px {im.Width}x{im.Height} {Hex(Fnv(im.Rgba))}"; }
        catch (Exception e) { return "px ERR " + Err(e); }
    }

    static void ToBytesLine(StringBuilder sb, PAKData d, string indent)
    {
        try { byte[] t = d.ToBytes(); sb.Append($"{indent}tobytes {Hex(Fnv(t))} {t.Length}\n"); }
        catch (Exception e) { sb.Append($"{indent}tobytes ERR {Err(e)}\n"); }
    }

    static void Repair(StringBuilder sb, string label, Func<PAKRepairReport> run)
    {
        try
        {
            PAKRepairReport r = run();
            sb.Append($"{label} success={(r.Success ? 1 : 0)} placeholders={string.Join(",", r.PlaceholderSprites)}\n");
            foreach (var i in r.Issues) sb.Append("  " + i + "\n");
            foreach (var s in r.Data.Sprites) sb.Append("  " + SpriteLine(s) + "\n");
            ToBytesLine(sb, r.Data, "  ");
        }
        catch (Exception e) { sb.Append($"{label} ERR {Err(e)}\n"); }
    }

    static void Pak(StringBuilder sb, byte[] bytes)
    {
        try
        {
            PAKData d = PAKData.Read(bytes);
            sb.Append($"read OK n={d.Sprites.Count}\n");
            foreach (var s in d.Sprites) sb.Append("  " + SpriteLine(s) + " " + Pixels(s.data) + "\n");
            ToBytesLine(sb, d, "");
        }
        catch (Exception e) { sb.Append("read ERR " + Err(e) + "\n"); }

        Repair(sb, "repair whole force=0", () => PAKRepair.Repair(bytes, false));
        Repair(sb, "repair whole force=1", () => PAKRepair.Repair(bytes, true));
        int n = bytes.Length;
        int te = 24;
        if (n >= 24) { int c = BitConverter.ToInt32(bytes, 20); if (c >= 0 && 24 + (long)c * 8 <= n) te = 24 + c * 8; }
        if (te > n) te = n;
        int two = (int)(2L * n / 3) < te ? te : (int)(2L * n / 3);
        int[][] w = { new[] { te, n }, new[] { n / 3, n }, new[] { te, two }, new[] { n / 5, (int)(4L * n / 5) } };
        foreach (var p in w)
            Repair(sb, $"repair section {p[0]}-{p[1]} force=1", () => PAKRepair.Repair(bytes, p[0], p[1], true));
    }

    static void Img(StringBuilder sb, byte[] bytes)
    {
        sb.Append($"format {SpriteImage.GetFormat(bytes)} bpp={SpriteImage.GetBmpBitsPerPixel(bytes)}\n");
        bool ok = SpriteImage.TryGetSize(bytes, out int w, out int h);
        sb.Append($"size {(ok ? 1 : 0)} {w}x{h}\n");
        try { SpriteImage.CheckSize(bytes); sb.Append("check OK\n"); }
        catch (Exception e) { sb.Append("check ERR " + Err(e) + "\n"); }
        sb.Append(Pixels(bytes) + "\n");
        foreach (uint? key in new uint?[] { null, 0xFF123456u, 0xFFFF00FFu })
        {
            try { byte[] b = SpriteImage.ToBmp24(bytes, key); sb.Append($"tobmp {Key(key)} {Hex(Fnv(b))} {b.Length}\n"); }
            catch (Exception e) { sb.Append($"tobmp {Key(key)} ERR {Err(e)}\n"); }
        }
        foreach (uint? key in new uint?[] { null, 0xFF000000u })
        {
            try
            {
                byte[] png = SpriteImage.ToPng(bytes, key, out uint? used);
                var im = SpriteImage.Decode(png);
                sb.Append($"topng {Key(key)} used={Key(used)} {im.Width}x{im.Height} {Hex(Fnv(im.Rgba))}\n");
            }
            catch (Exception e) { sb.Append($"topng {Key(key)} ERR {Err(e)}\n"); }
        }
    }

    static int Main(string[] a)
    {
        if (a.Length < 3)
        {
            Console.Error.WriteLine("usage: ParityHarness <cases dir> <list file> <report file>");
            return 2;
        }
        var sb = new StringBuilder();
        foreach (string line in File.ReadAllLines(a[1]))
        {
            string name = line.Trim();
            if (name.Length == 0) continue;
            byte[] bytes = File.ReadAllBytes(Path.Combine(a[0], name));
            sb.Append("== " + name + "\n");
            if (name.EndsWith(".img", StringComparison.Ordinal)) Img(sb, bytes);
            else Pak(sb, bytes);
        }
        // PAKLib's messages are ASCII (bytes above 0x7F already read as '?'), but echoed PNG chunk types can hold
        // control characters: write each as \xHH so the report stays a text file (ParityReport.h does the same)
        var text = new StringBuilder();
        foreach (char c in sb.ToString())
        {
            if (c < 0x20 && c != '\n') text.Append("\\x").Append(((int)c).ToString("X2"));
            else text.Append(c);
        }
        File.WriteAllText(a[2], text.ToString(), new UTF8Encoding(false));
        return 0;
    }
}
