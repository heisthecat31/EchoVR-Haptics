import tkinter as tk
from tkinter import ttk, messagebox
import os
import sys
import json
import shutil
from tkinter import filedialog

CONFIG_FILENAME = "haptics_config.txt"
APP_CONFIG = "config.json"

def get_resource_path(relative_path):
    """ Get absolute path to resource, works for dev and for PyInstaller """
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)

IMAGE_FILE = get_resource_path("controllers.png")
ICON_FILE = get_resource_path("george.ico")

COLORS = {
    "bg": "#0f0f0f",
    "card": "#1a1a1a",
    "text": "#ffffff",
    "accent_cyan": "#00d4ff",
    "accent_orange": "#ff8c00",
    "dim_text": "#aaaaaa",
    "border": "#333333"
}

# Left: X, Stick, Y, Trigger, Grip
# Right: B, Stick, A, Trigger, Grip
LEFT_COMPONENTS = ["X", "Y", "LStick", "LTrigger", "LGrip"]
RIGHT_COMPONENTS = ["A", "B", "RStick", "RTrigger", "RGrip"]
ALL_MAPPABLE = [
    "A", "B", "X", "Y", "LStick", "RStick", "Menu", 
    "LTrigger", "RTrigger", "LGrip", "RGrip"
]

class VisualConfigurator:
    def __init__(self, root):
        self.root = root
        self.root.title("EchoVR Support Modding Tool")
        self.root.geometry("850x800")
        self.root.configure(bg=COLORS["bg"])
        
        # Set Icon
        if os.path.exists(ICON_FILE):
            try:
                self.root.iconbitmap(ICON_FILE)
            except: pass
        
        self.haptic_val = tk.DoubleVar(value=1.4)
        self.fov_val = tk.DoubleVar(value=1.0)
        self.stick_mode = tk.StringVar(value="0")
        
        self.mappings = {name: tk.StringVar(value=name) for name in ALL_MAPPABLE}
        
        self.game_path = self.get_game_path()
        self.config_path = os.path.join(self.game_path, CONFIG_FILENAME)
        
        self.load_config()
        self.setup_ui()
        self.deploy_dll()

    def get_game_path(self):
        path = None
        if os.path.exists(APP_CONFIG):
            try:
                with open(APP_CONFIG, "r") as f:
                    data = json.load(f)
                    path = data.get("echo_vr_path")
            except: pass

        if not path or not os.path.exists(path):
            messagebox.showinfo("Setup", "Please select your Echo VR installation folder\n(e.g., ...\\ready-at-dawn-echo-arena)")
            path = filedialog.askdirectory(title="Select Echo VR Installation Folder")
            if not path:
                messagebox.showerror("Error", "Echo VR path is required. Exiting.")
                self.root.destroy()
                exit()
            
            if "bin\\win10" not in path.lower():
                potential_path = os.path.join(path, "bin", "win10")
                if os.path.exists(potential_path):
                    path = potential_path
                else:
                    if path.lower().endswith("ready-at-dawn-echo-arena"):
                        path = potential_path
            
            try:
                with open(APP_CONFIG, "w") as f:
                    json.dump({"echo_vr_path": path}, f, indent=4)
            except: pass
            
        return path

    def deploy_dll(self):
        source_dir = "plugins"
        source = os.path.join(source_dir, "dbgcore.dll")
        if not os.path.exists(source):
            source = "dbgcore.dll"
            
        if os.path.exists(source):
            dest = os.path.join(self.game_path, "dbgcore.dll")
            try:
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                if os.path.exists(dest):
                    os.remove(dest)
                shutil.copy2(source, dest)
            except Exception as e:
                messagebox.showerror("Deployment Error", f"Failed to move dbgcore.dll: {str(e)}")
        else:
            dest = os.path.join(self.game_path, "dbgcore.dll")
            if not os.path.exists(dest):
                messagebox.showwarning("Missing File", "dbgcore.dll not found in 'plugins' or root folder, and not found in Echo VR folder.")

    def load_config(self):
        if not os.path.exists(self.config_path):
            return
        try:
            with open(self.config_path, "r") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith(("#", "/")): continue
                    if "=" in line:
                        key, val = [x.strip() for x in line.split("=", 1)]
                        if key == "HapticStrength": self.haptic_val.set(float(val))
                        elif key == "FovMultiplier": self.fov_val.set(float(val))
                        elif key == "StickRemapMode": self.stick_mode.set(val)
                        elif key.startswith("Map_"):
                            btn = key[4:]
                            if btn in self.mappings:
                                self.mappings[btn].set(val)
            
        except: pass

    def save_config(self):
        try:
            lines = [
                "# EchoVR Haptics & FOV Mod Config\n",
                f"HapticStrength = {self.haptic_val.get():.2f}\n",
                f"FovMultiplier = {self.fov_val.get():.2f}\n",
                f"StickRemapMode = {self.stick_mode.get()}\n",
                "\n# Button & Analog Mappings\n"
            ]
            
            for name, var in self.mappings.items():
                target = var.get()
                lines.append(f"Map_{name} = {target}\n")
            
            with open(self.config_path, "w") as f:
                f.writelines(lines)
            
            self.deploy_dll()
            
            messagebox.showinfo("Success", "Configuration saved and mod files deployed!")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def setup_ui(self):
        top = tk.Frame(self.root, bg=COLORS["bg"], pady=10)
        top.pack(fill="x")
        tk.Label(top, text="CONTROLLER REMAPPING", font=("Segoe UI", 18, "bold"), fg=COLORS["accent_cyan"], bg=COLORS["bg"]).pack()

        canvas_frame = tk.Frame(self.root, bg=COLORS["bg"])
        canvas_frame.pack(fill="both", expand=True)
        
        self.canvas = tk.Canvas(canvas_frame, width=800, height=400, bg=COLORS["bg"], highlightthickness=0)
        self.canvas.pack(pady=20)

        try:
            self.bg_img = tk.PhotoImage(file=IMAGE_FILE)
            self.canvas.create_image(400, 200, image=self.bg_img)
        except Exception:
            self.canvas.create_text(400, 200, text="[ IMAGE NOT FOUND ]\nPlease save image as controllers.png", 
                                    fill=COLORS["dim_text"], font=("Segoe UI", 12))

        coords = {
            # Left Controller
            "X": (110, 125, "se"),
            "LStick": (195, 68, "s"),
            "Y": (270, 124, "sw"),
            "LTrigger": (310, 210, "w"),
            "LGrip": (140, 350, "n"),
            
            # Right Controller
            "B": (500, 123, "se"),
            "RStick": (610, 68, "s"),
            "A": (700, 125, "sw"),
            "RTrigger": (490, 210, "e"),
            "RGrip": (650, 350, "n"),
            
            # Additional (Menu button is usually between them or on one)
            "Menu": (395, 350, "n")
        }

        for name, (x, y, anchor) in coords.items():
            if name not in self.mappings: continue
            
            f = tk.Frame(self.canvas, bg=COLORS["card"], bd=1, highlightbackground=COLORS["border"])
            tk.Label(f, text=name, font=("Segoe UI", 8, "bold"), fg=COLORS["accent_cyan"], bg=COLORS["card"]).pack()
            cb = ttk.Combobox(f, textvariable=self.mappings[name], values=ALL_MAPPABLE, width=10, state="readonly")
            cb.pack(padx=2, pady=2)
            
            self.canvas.create_window(x, y, window=f, anchor=anchor)

        bottom = tk.Frame(self.root, bg=COLORS["bg"], padx=40)
        bottom.pack(fill="x", pady=20)

        # Haptics
        h_frame = tk.Frame(bottom, bg=COLORS["bg"])
        h_frame.pack(side="left", fill="x", expand=True)
        tk.Label(h_frame, text="HAPTIC STRENGTH", fg=COLORS["dim_text"], bg=COLORS["bg"], font=("Segoe UI", 9, "bold")).pack(anchor="w")
        tk.Scale(h_frame, from_=0.0, to=5.0, resolution=0.1, orient="horizontal", variable=self.haptic_val,
                 bg=COLORS["bg"], fg=COLORS["text"], highlightthickness=0, troughcolor=COLORS["border"]).pack(fill="x", padx=(0, 20))

        # FOV
        f_frame = tk.Frame(bottom, bg=COLORS["bg"])
        f_frame.pack(side="left", fill="x", expand=True)
        tk.Label(f_frame, text="FOV MULTIPLIER", fg=COLORS["dim_text"], bg=COLORS["bg"], font=("Segoe UI", 9, "bold")).pack(anchor="w")
        tk.Scale(f_frame, from_=0.5, to=2.0, resolution=0.05, orient="horizontal", variable=self.fov_val,
                 bg=COLORS["bg"], fg=COLORS["text"], highlightthickness=0, troughcolor=COLORS["border"]).pack(fill="x")

        # Stick Remap Mode
        s_frame = tk.Frame(self.root, bg=COLORS["card"], bd=1, highlightbackground=COLORS["border"], padx=20, pady=10)
        s_frame.pack(fill="x", padx=40, pady=10)
        
        tk.Label(s_frame, text="STICK REMAP MODE", font=("Segoe UI", 10, "bold"), fg=COLORS["accent_cyan"], bg=COLORS["card"]).pack(side="left")
        
        modes = [("Both", "0"), ("Turning Only", "1"), ("Buttons Only", "2")]
        for text, val in modes:
            tk.Radiobutton(s_frame, text=text, variable=self.stick_mode, value=val, 
                           bg=COLORS["card"], fg=COLORS["text"], selectcolor=COLORS["border"],
                           activebackground=COLORS["card"], activeforeground=COLORS["accent_cyan"],
                           font=("Segoe UI", 9)).pack(side="left", padx=15)

        # Save Button
        save_btn = tk.Button(self.root, text="SAVE ALL CHANGES", font=("Segoe UI", 12, "bold"), 
                             bg=COLORS["accent_cyan"], fg=COLORS["bg"], activebackground=COLORS["accent_orange"],
                             command=self.save_config, relief="flat", pady=10)
        save_btn.pack(fill="x", side="bottom", padx=40, pady=10)

        tk.Label(self.root, text="Made by he_is_the_cat", font=("Segoe UI", 9, "italic"), 
                 fg=COLORS["dim_text"], bg=COLORS["bg"]).pack(side="bottom", pady=5)

if __name__ == "__main__":
    root = tk.Tk()
    style = ttk.Style()
    style.theme_use('clam')
    style.configure("TCombobox", fieldbackground=COLORS["card"], background=COLORS["border"], foreground=COLORS["text"], arrowcolor=COLORS["accent_cyan"])
    app = VisualConfigurator(root)
    root.mainloop()
