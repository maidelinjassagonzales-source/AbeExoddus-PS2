// Minimal ISO 9660 (level 1) writer laid out the way a PS2 DVD needs it:
// the PS2 BIOS reads the L path table from sector 257 on DVD media, so the path tables go there.
// Identical files (same size and MD5) are stored once.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

public class Ps2Iso
{
    const int S = 2048;

    class Dir
    {
        public string Name; public Dir Parent; public int Number;
        public SortedDictionary<string, Dir> Dirs = new SortedDictionary<string, Dir>(StringComparer.Ordinal);
        public SortedDictionary<string, FileEntry> Files = new SortedDictionary<string, FileEntry>(StringComparer.Ordinal);
        public uint Lba; public uint Size;
    }
    class FileEntry { public string Path; public byte[] Data; public long Length; public uint Lba; public string Key; }

    readonly Dir root = new Dir { Name = "" };
    readonly List<FileEntry> order = new List<FileEntry>();

    Dir GetDir(string path)
    {
        Dir d = root;
        foreach (var part in path.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries))
        {
            Dir c;
            if (!d.Dirs.TryGetValue(part, out c)) { c = new Dir { Name = part, Parent = d }; d.Dirs[part] = c; }
            d = c;
        }
        return d;
    }

    public void AddFile(string dir, string name, byte[] data)
    {
        var f = new FileEntry { Data = data, Length = data.Length };
        GetDir(dir).Files[name + ";1"] = f; order.Add(f);
    }

    public void AddFilePath(string dir, string name, string path)
    {
        var f = new FileEntry { Path = path, Length = new FileInfo(path).Length };
        GetDir(dir).Files[name + ";1"] = f; order.Add(f);
    }

    static int RecLen(int nameLen) { int l = 33 + nameLen; return l + (l & 1); }

    static uint DirSize(Dir d)
    {
        var lens = new List<int> { RecLen(1), RecLen(1) };
        lens.AddRange(d.Dirs.Keys.Select(n => RecLen(n.Length)));
        lens.AddRange(d.Files.Keys.Select(n => RecLen(n.Length)));
        int used = 0, sectors = 1;
        foreach (int l in lens) { if (used + l > S) { sectors++; used = 0; } used += l; }
        return (uint)(sectors * S);
    }

    List<Dir> PathOrder()
    {
        var list = new List<Dir> { root };
        for (int i = 0; i < list.Count; i++) list.AddRange(list[i].Dirs.Values);
        for (int i = 0; i < list.Count; i++) list[i].Number = i + 1;
        return list;
    }

    static void Both16(byte[] b, int o, int v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v; }
    static void Both32(byte[] b, int o, uint v)
    {
        BitConverter.GetBytes(v).CopyTo(b, o);
        var be = BitConverter.GetBytes(v); Array.Reverse(be); be.CopyTo(b, o + 4);
    }
    static void RecDate(byte[] b, int o, DateTime t)
    {
        b[o] = (byte)(t.Year - 1900); b[o + 1] = (byte)t.Month; b[o + 2] = (byte)t.Day;
        b[o + 3] = (byte)t.Hour; b[o + 4] = (byte)t.Minute; b[o + 5] = (byte)t.Second; b[o + 6] = 0;
    }
    static void Text(byte[] b, int o, int len, string s)
    {
        for (int i = 0; i < len; i++) b[o + i] = (byte)(i < s.Length ? s[i] : ' ');
    }

    static int Record(byte[] b, int o, string name, uint lba, uint size, bool dir, DateTime t)
    {
        byte[] id = name == "\0" ? new byte[] { 0 } : name == "\u0001" ? new byte[] { 1 } : Encoding.ASCII.GetBytes(name);
        int len = RecLen(id.Length);
        b[o] = (byte)len;
        Both32(b, o + 2, lba); Both32(b, o + 10, size);
        RecDate(b, o + 18, t);
        b[o + 25] = (byte)(dir ? 2 : 0);
        Both16(b, o + 28, 1);
        b[o + 32] = (byte)id.Length;
        id.CopyTo(b, o + 33);
        return len;
    }

    static string Hash(FileEntry f)
    {
        using (var md5 = MD5.Create())
        {
            byte[] h;
            if (f.Data != null) h = md5.ComputeHash(f.Data);
            else using (var s = File.OpenRead(f.Path)) h = md5.ComputeHash(s);
            return f.Length + ":" + BitConverter.ToString(h);
        }
    }

    public void Write(string outPath)
    {
        var t = DateTime.Now;
        var dirs = PathOrder();

        // Path tables: L at 257, M right after (both fit in one sector each for a disc this size).
        int ptSize = dirs.Sum(d => 8 + Math.Max(1, d.Name.Length) + (Math.Max(1, d.Name.Length) & 1));
        if (ptSize > S) throw new Exception("path table too large");
        uint next = 259;
        foreach (var d in dirs) { d.Size = DirSize(d); d.Lba = next; next += d.Size / S; }

        // Files: SYSTEM.CNF and the ELF first (they were added first), duplicates share one copy.
        var stored = new Dictionary<string, FileEntry>();
        foreach (var f in order)
        {
            f.Key = Hash(f);
            FileEntry same;
            if (stored.TryGetValue(f.Key, out same)) { f.Lba = same.Lba; continue; }
            stored[f.Key] = f;
            f.Lba = next;
            next += (uint)((f.Length + S - 1) / S);
        }
        uint total = next;

        using (var fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
        {
            var sector = new byte[S];
            // System area 0-15
            for (int i = 0; i < 16; i++) fs.Write(sector, 0, S);
            // PVD
            var pvd = new byte[S];
            pvd[0] = 1; Text(pvd, 1, 5, "CD001"); pvd[6] = 1;
            Text(pvd, 8, 32, "PLAYSTATION"); Text(pvd, 40, 32, "ABE_EXODDUS");
            Both32(pvd, 80, total); Both16(pvd, 120, 1); Both16(pvd, 124, 1); Both16(pvd, 128, S);
            Both32(pvd, 132, (uint)ptSize);
            BitConverter.GetBytes(257u).CopyTo(pvd, 140);
            var m = BitConverter.GetBytes(258u); Array.Reverse(m); m.CopyTo(pvd, 148);
            Record(pvd, 156, "\0", root.Lba, root.Size, true, t);
            Text(pvd, 190, 128, ""); Text(pvd, 318, 128, ""); Text(pvd, 446, 128, "");
            Text(pvd, 574, 128, "PLAYSTATION");
            Text(pvd, 702, 37, ""); Text(pvd, 739, 37, ""); Text(pvd, 776, 37, "");
            string d17 = t.ToString("yyyyMMddHHmmss") + "00";
            Text(pvd, 813, 16, d17); Text(pvd, 830, 16, d17); Text(pvd, 847, 16, "0000000000000000"); Text(pvd, 864, 16, "0000000000000000");
            pvd[881] = 1;
            fs.Write(pvd, 0, S);
            // Terminator
            var term = new byte[S]; term[0] = 255; Text(term, 1, 5, "CD001"); term[6] = 1;
            fs.Write(term, 0, S);
            // 18..256 empty
            Array.Clear(sector, 0, S);
            for (int i = 18; i < 257; i++) fs.Write(sector, 0, S);
            // Path tables
            for (int big = 0; big < 2; big++)
            {
                var pt = new byte[S]; int o = 0;
                foreach (var d in dirs)
                {
                    byte[] id = d == root ? new byte[] { 0 } : Encoding.ASCII.GetBytes(d.Name);
                    pt[o] = (byte)id.Length;
                    var lba = BitConverter.GetBytes(d.Lba); var par = BitConverter.GetBytes((ushort)(d.Parent == null ? 1 : d.Parent.Number));
                    if (big == 1) { Array.Reverse(lba); Array.Reverse(par); }
                    lba.CopyTo(pt, o + 2); par.CopyTo(pt, o + 6); id.CopyTo(pt, o + 8);
                    o += 8 + id.Length + (id.Length & 1);
                }
                fs.Write(pt, 0, S);
            }
            // Directories
            foreach (var d in dirs)
            {
                var buf = new byte[d.Size]; int o = 0;
                Action<string, uint, uint, bool> add = (name, lba, size, isDir) =>
                {
                    int nl = (name == "\0" || name == "\u0001") ? 1 : name.Length;
                    int l = RecLen(nl);
                    if ((o % S) + l > S) o = (o / S + 1) * S;
                    Record(buf, o, name, lba, size, isDir, t); o += l;
                };
                add("\0", d.Lba, d.Size, true);
                var p = d.Parent ?? d; add("\u0001", p.Lba, p.Size, true);
                // ISO 9660 order: by name, directories and files together
                var names = d.Dirs.Keys.Select(n => Tuple.Create(n, true)).Concat(d.Files.Keys.Select(n => Tuple.Create(n, false)))
                    .OrderBy(x => x.Item1, StringComparer.Ordinal);
                foreach (var x in names)
                {
                    if (x.Item2) { var c = d.Dirs[x.Item1]; add(x.Item1, c.Lba, c.Size, true); }
                    else { var f = d.Files[x.Item1]; add(x.Item1, f.Lba, (uint)f.Length, false); }
                }
                fs.Write(buf, 0, buf.Length);
            }
            // File data
            var copy = new byte[1 << 20];
            foreach (var f in stored.Values.OrderBy(x => x.Lba))
            {
                if (fs.Position != (long)f.Lba * S) throw new Exception("layout error at " + f.Lba);
                if (f.Data != null) fs.Write(f.Data, 0, f.Data.Length);
                else using (var src = File.OpenRead(f.Path)) { int n; while ((n = src.Read(copy, 0, copy.Length)) > 0) fs.Write(copy, 0, n); }
                int pad = (int)((S - f.Length % S) % S);
                if (pad > 0) fs.Write(new byte[pad], 0, pad);
            }
        }
    }
}
