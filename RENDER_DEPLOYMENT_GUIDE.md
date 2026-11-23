# FieldSense IoT Cloud — Render Deployment Guide 🚀

Follow these steps to host your FieldSense Agriculture IoT Dashboard on **Render** so you can access it from **any phone, laptop, or computer anywhere in the world**.

---

## 📌 Prerequisites
1. A free **GitHub account** ([github.com](https://github.com))
2. A free **Render account** ([render.com](https://render.com) — sign up with your GitHub account)

---

## Step 1: Create a Repository on GitHub

1. Open [github.com/new](https://github.com/new).
2. Set **Repository name**: `fieldsense-iot` (or any name you prefer).
3. Set visibility to **Public** (or **Private**).
4. Leave **"Add a README file"**, **".gitignore"**, and **"license"** **UNCHECKED** (we already created them locally).
5. Click **Create repository**.

---

## Step 2: Push Local Code to GitHub

Open PowerShell or Command Prompt in `D:\test ai jeet` and run (replace `<YOUR_GITHUB_USERNAME>` with your actual GitHub username):

```powershell
git remote add origin https://github.com/<YOUR_GITHUB_USERNAME>/fieldsense-iot.git
git push -u origin main
```

*(If prompted, log in with your GitHub credentials or Personal Access Token)*.

---

## Step 3: Deploy on Render

1. Log in to your [Render Dashboard](https://dashboard.render.com).
2. Click the blue **"New +"** button at the top right, then select **"Web Service"**.
3. Choose **"Build and deploy from a Git repository"** and click **Next**.
4. Connect your GitHub account (if not already connected) and select your **`fieldsense-iot`** repository.
5. Render will automatically read the included [`render.yaml`](file:///d:/test%20ai%20jeet/render.yaml) configuration, or you can verify these settings:
   - **Name**: `fieldsense-iot` (or any name, e.g., `fieldsense-smart-farm`)
   - **Region**: Choose closest to you (e.g., *Singapore*, *Frankfurt*, or *Oregon*)
   - **Branch**: `main`
   - **Runtime**: `Python 3`
   - **Build Command**: `pip install -r requirements.txt`
   - **Start Command**: `uvicorn main:app --host 0.0.0.0 --port $PORT`
   - **Instance Type**: **Free** ($0/month)
6. Under **Environment Variables**, ensure the following are present:
   | Key | Value |
   |---|---|
   | `PYTHON_VERSION` | `3.11.9` |
   | `DEVICE_TOKEN` | `FieldSenseSecretToken2026` |
   | `FARMER_USERNAME` | `farmer` |
   | `FARMER_PASSWORD` | `farm123` |
7. Click **"Deploy Web Service"** (or **"Create Web Service"**).

Render will build and start your application in about 1-2 minutes.

---

## Step 4: Access Your Dashboard from Anywhere!

Once deployed, Render gives you a public HTTPS URL at the top of the dashboard:
`https://fieldsense-iot.onrender.com` (or your chosen service name).

- **On Any Laptop/PC**: Open `https://<YOUR_SERVICE_NAME>.onrender.com` in Chrome/Edge/Firefox.
- **On Any Mobile Phone**: Open `https://<YOUR_SERVICE_NAME>.onrender.com` in Safari/Chrome.
- **Login Credentials**:
  - **Username**: `farmer`
  - **Password**: `farm123`

---

## Step 5: Update ESP32 Firmware to Send Telemetry to Render

1. Open [`d:/FieldSense_ESP32.ino`](file:///d:/FieldSense_ESP32.ino) in **Arduino IDE**.
2. Locate line 47:
   ```cpp
   // Change from local IP to your Render URL:
   const char* SERVER_URL = "https://fieldsense-iot.onrender.com";
   ```
   *(Replace with your actual Render URL, make sure it starts with `https://`)*.
3. Keep `WIFI_SSID` and `WIFI_PASSWORD` set to your 2.4GHz Wi-Fi or mobile hotspot with internet access.
4. Click **Upload** (Arrow icon) in Arduino IDE.
5. Open Serial Monitor (115200 baud). Once connected to Wi-Fi, you will see:
   ```text
   [WiFi] Connected! IP: 192.168.x.x
   [TELEMETRY] POST 200 OK -> Cloud Synced!
   ```

Your physical ESP32 sensors, physical push button, and relay module are now directly linked to your cloud dashboard on Render worldwide! 🌾
