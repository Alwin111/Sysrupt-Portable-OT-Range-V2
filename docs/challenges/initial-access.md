# Initial Access — Hacking into the IT Server (before the OT)

Before starting the OT challenges, we need to gain access to the industrial web portal running on the
target device. This is the IT foothold: get into the corporate/engineering console first, *then* pivot
to the OT protocols the rest of the challenges live on.

The web server is available at `10.0.1.1` on TCP port 80. Our first task is to discover a hidden page,
inspect its source code, and find the login credentials.

We can do this using either a web browser or `curl`.

---

## Step 1: Access the Web Server

### Method 1 — Web Browser

Open your browser and visit:

```text
http://10.0.1.1
```

### Method 2 — Terminal

```bash
curl -i http://10.0.1.1
```

This connects to the web server and displays its HTTP response — a plain corporate login gate.

---

## Step 2: Discover robots.txt

Websites use `robots.txt` to tell search-engine crawlers which paths they should or should not access.
Sometimes it also reveals interesting paths we can investigate.

### Method 1 — Web Browser

Visit:

```text
http://10.0.1.1/robots.txt
```

### Method 2 — Terminal

```bash
curl http://10.0.1.1/robots.txt
```

Look through the output. You should discover:

```text
User-agent: *
Disallow: /control
Disallow: /hist-config
```

`Disallow` is **not** access control — it's a polite request to crawlers, enforced by nothing. Those
paths are just as reachable as any other. `/control` is the one we want next.

---

## Step 3: Open the Hidden Control Page

### Method 1 — Web Browser

Visit:

```text
http://10.0.1.1/control
```

Once the page loads, press:

```text
Ctrl + U
```

This opens the page's HTML source code. Search through the source for the username and password — the
rendered page shows an "engineering maintenance notice" with no visible credentials.

### Method 2 — Terminal

Retrieve the page source directly:

```bash
curl -s http://10.0.1.1/control
```

Read through the HTML and look for the login credentials.

---

## Step 4: Find the Login Credentials

The `/control` page hides the credentials **in an HTML comment** — invisible on the rendered page, but
right there in the source:

```html
<!-- maintenance window: shared engineering account still active - eng / Rec0ncile!42 ... -->
```

So the credentials are:

| Field | Value |
|-------|-------|
| Username | `eng` |
| Password | `Rec0ncile!42` |

**Why are we doing this?** A webpage contains more than what we see on screen. Its HTML source can hold
comments, hidden elements, and other information not visible in the normal browser view. Here,
inspecting the source reveals the credentials needed to access the portal.

---

## Step 5: Log In to the Portal

### Method 1 — Web Browser

1. Go to the console root `http://10.0.1.1/`.
2. Enter the username `eng`.
3. Enter the password `Rec0ncile!42`.
4. Click **Sign In** — you land on the operator HMI (`/hmi`).

### Method 2 — Terminal using curl

The login form posts to `/login` with the fields `user` and `pass`. The `!` in the password must be
URL-encoded as `%21` in the form body.

```bash
# authenticate and save the session cookie
curl -i -c cookies.txt -X POST http://10.0.1.1/login \
  -d 'user=eng&pass=Rec0ncile%2142'

# use the session cookie to reach the protected operator HMI
curl -i -b cookies.txt http://10.0.1.1/hmi
```

The `cookies.txt` file stores the authenticated session, which you then present on protected pages.

---

## What We Learned

- `robots.txt` can reveal interesting website paths.
- `/control` is discovered through `robots.txt`.
- `Ctrl + U` opens the HTML source of a webpage.
- HTML source inspection can reveal information hidden from the normal page view (here, credentials in a
  comment).
- `curl` lets us perform the same investigation directly from the terminal.
- Cookies maintain an authenticated web session.

**Objective:** Discover `/control`, inspect its source, recover the login credentials (`eng` /
`Rec0ncile!42`), and access the industrial web portal — your IT foothold before the OT challenges.
