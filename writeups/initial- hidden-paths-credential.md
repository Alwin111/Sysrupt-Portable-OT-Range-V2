# Challenge 9 — Hidden in Plain Sight

**Protocol:** HTTP (OSINT/recon style) · **Port:** 80/tcp

## The Story

`robots.txt` is meant to tell search engines what *not* to crawl — which means it's also a
public list of paths the site owner didn't want indexed, handed to you for free.

## What You Know

```
User-agent: *
Disallow: /control
Disallow: /hist-config
```

- `Disallow` entries are not access control — they're a polite request to crawlers, enforced by
  nothing. Anything listed there is just as reachable as any other page.
- Two paths are disclosed: `/control` and `/hist-config`.

---

## Step 1: Check robots.txt yourself

<details>
<summary>Hint</summary>

Always check this file first on any web recon — it's free information with zero risk.
</details>

<details>
<summary>Show command</summary>

```bash
curl http://10.0.1.1/robots.txt
```
</details>

## Step 2: Visit the disclosed paths directly

<details>
<summary>Hint</summary>

A `Disallow` entry is a map, not a lock. Just request the page.
</details>

<details>
<summary>Show command</summary>

```bash
curl -i http://10.0.1.1/control
curl -i http://10.0.1.1/hist-config
```

Note what each returns — a login form, a redirect, a 403, or something else entirely.
</details>

## Step 3: Look at what the login page gives away

<details>
<summary>Hint</summary>

Before trying to guess credentials, read the page source, any comments, linked JS/CSS filenames,
and response headers. CTF pages often leak more in the markup than in the visible form.
</details>

<details>
<summary>Show command</summary>

```bash
curl -s http://10.0.1.1/control | less
curl -s http://10.0.1.1/control | grep -i -E "user|pass|admin|TODO|note"
```
</details>

## Step 4: Figure out the actual credentials

<details>
<summary>Hint</summary>

Checkout the engineering note, every instruction and note is important.
</details>

<details>
<summary>Show command</summary>
  
```bash
username: eng
password: Rec0ncile!42
```

</details>

## What You Learned

- `robots.txt` as a reconnaissance source, not an access-control mechanism.
- Checking page source/headers for leaked hints before attempting credential guesses.
- Importance of engineering note
