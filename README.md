# 🚀 YuB-X Roblox API  
**Version:** `version-9bf2d7ce6a0345d5`  
**UNC:** 69

---

## 💉 Injection Guide

1. **Download the tools:**
   - [YuB-X MMP Injector](https://github.com/YuB-W/YuBX-RBX-MMP_V2)
   - [YuB-X Dumper](https://github.com/YuB-W/YuB-X_RBX_Dumper)

2. **Run Roblox** and inject `yubx.dll` using the YuB-X MMP Injector.

3. **Once injected,** you can send any Luau script to be executed remotely using the C# client example below.

---

## 📡 Sending Scripts from C# (.NET)

```csharp
using System;
using System.Net.Sockets;
using System.Text;

class ScriptSender
{
    const string Host = "127.0.0.1"; // Change to target IP if remote
    const int Port = 5454;

    static void Main(string[] args)
    {
        string script = args.Length > 0
            ? string.Join(" ", args)
            : "-- YuB-X Payload\nprint('Injected successfully!')";

        try
        {
            using (TcpClient client = new TcpClient(Host, Port))
            using (NetworkStream stream = client.GetStream())
            {
                byte[] scriptBytes = Encoding.UTF8.GetBytes(script);
                int scriptLength = scriptBytes.Length;

                // Send script length in big-endian
                byte[] lengthBytes = BitConverter.GetBytes(scriptLength);
                if (BitConverter.IsLittleEndian)
                    Array.Reverse(lengthBytes);

                stream.Write(lengthBytes, 0, 4);
                stream.Write(scriptBytes, 0, scriptBytes.Length);

                Console.WriteLine("[✓] Script sent to YuB-X server");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("[✗] Failed to send script: " + ex.Message);
        }
    }
}

