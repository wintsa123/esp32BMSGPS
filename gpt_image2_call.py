import requests
import base64
import sys
import time
import os
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

API_BASE = "https://image.squarefaceicon.org"
LOCAL_API_KEY_FILE = ".gpt_image2_api_key"


def _load_api_key():
    env_key = os.environ.get("GPT_IMAGE2_API_KEY", "").strip()
    if env_key:
        return env_key
    try:
        with open(LOCAL_API_KEY_FILE, "r", encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


API_KEY = _load_api_key()
MODEL = "gpt-image-2"


def generate_image(prompt, size="1024x1024", quality="medium", n=1, output_dir=".",
                   api_key=None, log=print, ref_images=None):
    key = api_key or API_KEY
    if ref_images:
        url = f"{API_BASE}/v1/images/edits"
        headers = {"Authorization": f"Bearer {key}"}
        files = []
        for img_path in ref_images:
            files.append(("image[]", (os.path.basename(img_path), open(img_path, "rb"), "image/png")))
        data = {
            "model": MODEL,
            "prompt": prompt,
            "size": size,
            "n": str(n),
            "quality": quality,
        }
        log(f"参考图: {len(ref_images)} 张")
        log(f"Prompt: {prompt}")
        log(f"Model: {MODEL} | Size: {size} | Quality: {quality} | N: {n}")
        log("生成中（约 30~90 秒）...")
        t0 = time.time()
        try:
            r = requests.post(url, headers=headers, files=files, data=data, timeout=180)
        except Exception as e:
            log(f"请求异常: {e}")
            return []
        elapsed = time.time() - t0
        log(f"Status: {r.status_code} | 耗时 {elapsed:.1f}s")
        if r.status_code != 200:
            log(f"Error: {r.text[:500]}")
            return []
        data = r.json()
    else:
        url = f"{API_BASE}/v1/images/generations"
        headers = {
            "Authorization": f"Bearer {key}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": MODEL,
            "prompt": prompt,
            "size": size,
            "n": n,
            "quality": quality,
        }
        log(f"Prompt: {prompt}")
        log(f"Model: {MODEL} | Size: {size} | Quality: {quality} | N: {n}")
        log("生成中（约 30~90 秒）...")
        t0 = time.time()
        try:
            r = requests.post(url, headers=headers, json=payload, timeout=180)
        except Exception as e:
            log(f"请求异常: {e}")
            return []
        elapsed = time.time() - t0
        log(f"Status: {r.status_code} | 耗时 {elapsed:.1f}s")
        if r.status_code != 200:
            log(f"Error: {r.text[:500]}")
            return []
        data = r.json()
    saved = []
    os.makedirs(output_dir, exist_ok=True)
    for idx, item in enumerate(data.get("data", [])):
        b64 = item.get("b64_json")
        if not b64:
            continue
        ts = int(time.time())
        fn = os.path.join(output_dir, f"image_{ts}_{idx}.png")
        raw = base64.b64decode(b64)
        with open(fn, "wb") as f:
            f.write(raw)
        size_kb = len(raw) // 1024
        log(f"已保存: {fn} ({size_kb} KB)")
        saved.append(fn)
    return saved


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("GPT-Image-2 生成器")
        self.geometry("640x560")

        pad = {"padx": 8, "pady": 4}

        # API Key
        row = 0
        ttk.Label(self, text="API Key:").grid(row=row, column=0, sticky="w", **pad)
        self.key_var = tk.StringVar(value=API_KEY)
        ttk.Entry(self, textvariable=self.key_var, show="*", width=60).grid(
            row=row, column=1, columnspan=3, sticky="we", **pad)

        # Prompt
        row += 1
        ttk.Label(self, text="Prompt:").grid(row=row, column=0, sticky="nw", **pad)
        self.prompt_text = tk.Text(self, height=5, width=60, wrap="word")
        self.prompt_text.insert("1.0", "一只穿着宇航服的猫在月球上行走，电影质感")
        self.prompt_text.grid(row=row, column=1, columnspan=3, sticky="we", **pad)

        # Size / Quality / N
        row += 1
        ttk.Label(self, text="Size:").grid(row=row, column=0, sticky="w", **pad)
        self.size_var = tk.StringVar(value="1024x1024")
        ttk.Combobox(self, textvariable=self.size_var, width=14,
                     values=["512x512", "1024x1024", "1024x1536", "1536x1024"]).grid(
            row=row, column=1, sticky="w", **pad)

        ttk.Label(self, text="Quality:").grid(row=row, column=2, sticky="w", **pad)
        self.quality_var = tk.StringVar(value="medium")
        ttk.Combobox(self, textvariable=self.quality_var, width=10,
                     values=["low", "medium", "high"]).grid(
            row=row, column=3, sticky="w", **pad)

        row += 1
        ttk.Label(self, text="N (张数):").grid(row=row, column=0, sticky="w", **pad)
        self.n_var = tk.IntVar(value=1)
        ttk.Spinbox(self, from_=1, to=10, textvariable=self.n_var, width=5).grid(
            row=row, column=1, sticky="w", **pad)

        # Output dir
        row += 1
        ttk.Label(self, text="输出目录:").grid(row=row, column=0, sticky="w", **pad)
        self.outdir_var = tk.StringVar(value=os.path.abspath("."))
        ttk.Entry(self, textvariable=self.outdir_var, width=45).grid(
            row=row, column=1, columnspan=2, sticky="we", **pad)
        ttk.Button(self, text="选择...", command=self.choose_dir).grid(
            row=row, column=3, sticky="w", **pad)

        # Reference images
        row += 1
        ttk.Label(self, text="参考图:").grid(row=row, column=0, sticky="nw", **pad)
        self.ref_text = tk.Text(self, height=3, width=45, wrap="word")
        self.ref_text.grid(row=row, column=1, columnspan=2, sticky="we", **pad)
        ref_btn_frame = ttk.Frame(self)
        ref_btn_frame.grid(row=row, column=3, sticky="nw", **pad)
        ttk.Button(ref_btn_frame, text="添加...", command=self.choose_ref_images).pack(fill="x")
        ttk.Button(ref_btn_frame, text="清空", command=self.clear_ref_images).pack(fill="x", pady=(2, 0))

        # Buttons
        row += 1
        self.gen_btn = ttk.Button(self, text="生成图像", command=self.on_generate)
        self.gen_btn.grid(row=row, column=1, sticky="w", **pad)
        ttk.Button(self, text="清空日志", command=self.clear_log).grid(
            row=row, column=2, sticky="w", **pad)

        # Log
        row += 1
        ttk.Label(self, text="日志:").grid(row=row, column=0, sticky="nw", **pad)
        self.log_text = tk.Text(self, height=15, width=70, state="disabled")
        self.log_text.grid(row=row, column=1, columnspan=3, sticky="nsew", **pad)

        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(row, weight=1)

    def choose_dir(self):
        d = filedialog.askdirectory(initialdir=self.outdir_var.get() or ".")
        if d:
            self.outdir_var.set(d)

    def choose_ref_images(self):
        files = filedialog.askopenfilenames(
            title="选择参考图",
            filetypes=[("Image files", "*.png *.jpg *.jpeg")],
            initialdir="."
        )
        if files:
            current = self.ref_text.get("1.0", "end").strip()
            existing = [p for p in current.split("\n") if p.strip()] if current else []
            existing.extend(files)
            self.ref_text.delete("1.0", "end")
            self.ref_text.insert("1.0", "\n".join(existing))

    def clear_ref_images(self):
        self.ref_text.delete("1.0", "end")

    def log(self, msg):
        def _append():
            self.log_text.configure(state="normal")
            self.log_text.insert("end", str(msg) + "\n")
            self.log_text.see("end")
            self.log_text.configure(state="disabled")
        self.after(0, _append)

    def clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def on_generate(self):
        key = self.key_var.get().strip()
        prompt = self.prompt_text.get("1.0", "end").strip()
        if not key:
            messagebox.showwarning("提示", "请填入 API Key")
            return
        if not prompt:
            messagebox.showwarning("提示", "请填入 Prompt")
            return

        self.gen_btn.configure(state="disabled", text="生成中...")

        def worker():
            try:
                ref_lines = self.ref_text.get("1.0", "end").strip()
                ref_images = [p.strip() for p in ref_lines.split("\n") if p.strip()] if ref_lines else None
                files = generate_image(
                    prompt=prompt,
                    size=self.size_var.get(),
                    quality=self.quality_var.get(),
                    n=int(self.n_var.get()),
                    output_dir=self.outdir_var.get() or ".",
                    api_key=key,
                    log=self.log,
                    ref_images=ref_images,
                )
                if files:
                    self.log(f"完成，共 {len(files)} 张。")
            finally:
                self.after(0, lambda: self.gen_btn.configure(
                    state="normal", text="生成图像"))

        threading.Thread(target=worker, daemon=True).start()


if __name__ == "__main__":
    # 支持两种用法：
    #   python gpt_image2_call.py               -> 启动 UI
    #   python gpt_image2_call.py --cli "prompt" -> 命令行模式
    if len(sys.argv) > 1 and sys.argv[1] == "--cli":
        if not API_KEY:
            print("请先设置 GPT_IMAGE2_API_KEY 环境变量")
            sys.exit(1)
        prompt = " ".join(sys.argv[2:]) or "一只穿着宇航服的猫在月球上行走，电影质感"
        generate_image(prompt)
    else:
        App().mainloop()
