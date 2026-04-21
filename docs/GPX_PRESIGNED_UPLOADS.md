# GPX WEB Upload With Presigned S3 URLs

## Purpose

The GPX WEB upload path now uses a plain HTTP `PUT` request.

The device no longer calculates AWS SigV4 headers locally. That means the target URL must already be authorized before the upload starts.

There are only two practical ways to do that:

1. Give the device one exact presigned `PUT` URL for one exact GPX file.
2. Have the device request a fresh presigned `PUT` URL from a trusted backend before each upload.

## Current RallyBox Context

Current GPX settings in `sdkconfig` are effectively:

- Bucket: `amzn-s3-rallybox-bucket-911`
- Region: `eu-central-2`
- Prefix: `tracks/rallyboxid/`
- Current unsiged URL template: `https://s3.eu-central-2.amazonaws.com/amzn-s3-rallybox-bucket-911/tracks/rallyboxid/{filename}`

Typical final object key looks like this:

```text
tracks/rallyboxid/24042026-153000.ble.gpx
```

Typical final S3 object URL looks like this:

```text
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx
```

## Important Constraint

A presigned S3 URL is not a reusable template.

It is cryptographically bound to:

- The HTTP method, here `PUT`
- The exact bucket
- The exact object key
- The expiration time
- The signed headers

Because of that, `{filename}` cannot stay variable after signing.

This is the key rule:

```text
first choose the final filename
then build the exact final object key
then generate the presigned URL for that exact key
```

The same applies to a presigned query string. A query string generated for one object path does not authorize other filenames.

## Option 1: One-Off Test URL

Use this when the goal is only to prove that the new firmware path works.

### Process

1. Pick one exact filename that the device will upload.
2. Generate one presigned `PUT` URL for that exact S3 object key.
3. Put that full URL directly into `CONFIG_RALLYBOX_GPX_WEB_UPLOAD_URL`.
4. Leave `CONFIG_RALLYBOX_GPX_S3_PRESIGNED_QUERY` empty.

### Example

If the filename is:

```text
24042026-153000.ble.gpx
```

Then the S3 object key is:

```text
tracks/rallyboxid/24042026-153000.ble.gpx
```

And the generated full URL will look like:

```text
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=...&X-Amz-Date=...&X-Amz-Expires=3600&X-Amz-SignedHeaders=host&X-Amz-Signature=...
```

That full value can be used directly as the upload URL for one test.

## Option 2: Real Production Flow

Use this when filenames change every time, which is the normal RallyBox behavior.

### Process

1. The device determines the filename.
2. A trusted backend receives that filename.
3. The backend generates a presigned `PUT` URL for the exact object key.
4. The backend returns the full presigned URL to the device.
5. The device uploads the GPX file with a normal `PUT`.

This is the correct design if uploads happen repeatedly with different names.

For a concrete AWS implementation, see [docs/AWS_LAMBDA_GPX_SIGNER.md](docs/AWS_LAMBDA_GPX_SIGNER.md).

## Firmware Signer Mode

The firmware now supports a dedicated signer step before upload.

If `CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL` is set, RallyBox does this:

1. Generate the final GPX filename.
2. Request a presigned upload URL from the signer endpoint.
3. Upload the GPX with a normal `PUT` to the returned URL.

This signer URL takes precedence over the older static upload URL mode.

### Signer URL Template

The signer URL may be configured in either of these forms:

```text
https://example.com/api/gpx-sign?filename={filename}
```

or:

```text
https://example.com/api/gpx-sign/
```

In the second form, RallyBox appends the generated filename.

### Expected Signer Response

The signer response may be either:

1. Plain text containing the full presigned upload URL.
2. JSON containing either `upload_url` or `url`.

Example plain text response:

```text
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&...
```

Example JSON response:

```json
{
  "upload_url": "https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&..."
}
```

Optional signer authentication can be configured with:

- `CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER`
- `CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE`

## Tomorrow Setup Checklist

Use this if the goal is to continue tomorrow without rethinking the configuration.

### Recommended Menuconfig Values

Set these values in `menuconfig` under the GPX WEB signer settings.

#### 1. GPX signer URL

Recommended value:

```text
https://your-domain.example/api/gpx-sign?filename={filename}
```

Why this format is recommended:

- RallyBox already generates the final filename before upload.
- The backend receives the exact filename it must authorize.
- Query string format is easier to inspect in logs and easier to test in a browser or with `curl`.

Alternative supported format:

```text
https://your-domain.example/api/gpx-sign/
```

In that form, RallyBox appends the generated filename automatically.

#### 2. GPX signer auth header

Recommended starter value:

```text
x-api-key
```

Use this unless an existing backend already expects something else.

#### 3. GPX signer auth value

Recommended starter value:

```text
<one strong random secret generated and stored on your backend>
```

Examples of acceptable secrets:

- a long random hex string
- a long random base64 string
- an API key issued by your backend

Do not use:

- a short guessable word
- a device name
- bucket name
- anything checked into source control

### If You Already Have Token Auth

If your server already uses bearer tokens instead of an API key, use:

```text
CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER = Authorization
CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE = Bearer <token>
```

### Suggested First-Pass Configuration

For the simplest first working setup, use:

```text
CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL = https://your-domain.example/api/gpx-sign?filename={filename}
CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER = x-api-key
CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE = <your-random-secret>
```

### What The Backend Endpoint Must Do

For a request like:

```text
GET /api/gpx-sign?filename=20042026-163250.ble.gpx
```

the backend should:

1. Validate the caller.
2. Validate the filename format.
3. Build the exact S3 object key.
4. Generate one presigned `PUT` URL for that exact key.
5. Return that full URL to the device.

Example object key:

```text
tracks/rallyboxid/20042026-163250.ble.gpx
```

### Accepted Backend Response Formats

RallyBox currently accepts any of these signer responses.

Plain text:

```text
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/20042026-163250.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&...
```

JSON with `upload_url`:

```json
{
  "upload_url": "https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/20042026-163250.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&..."
}
```

JSON with `url`:

```json
{
  "url": "https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/20042026-163250.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&..."
}
```

### Tomorrow Order Of Work

Recommended order:

1. Set the three signer values in `menuconfig`.
2. Decide the exact backend URL shape.
3. Implement the signer endpoint.
4. Test the signer endpoint from a PC first.
5. Flash RallyBox and test BLE WEB upload.

### PC Test Before Flashing

First verify that the signer endpoint returns a full presigned URL.

Example:

```bash
curl \
  -H "x-api-key: YOUR_SECRET_HERE" \
  "https://your-domain.example/api/gpx-sign?filename=20042026-163250.ble.gpx"
```

That response should be either:

- a full HTTPS presigned upload URL
- or JSON containing that URL

Then test the returned URL directly:

```bash
curl -X PUT \
  -H "Content-Type: application/gpx+xml" \
  --data-binary "@test.gpx" \
  "PASTE_RETURNED_PRESIGNED_URL_HERE"
```

If that works on a PC, the RallyBox upload path should be in good shape.

## Generating A Presigned URL With Python

The simplest generator is `boto3` on a trusted machine or server.

```python
import boto3

bucket = "amzn-s3-rallybox-bucket-911"
region = "eu-central-2"
filename = "24042026-153000.ble.gpx"
key = f"tracks/rallyboxid/{filename}"

s3 = boto3.client("s3", region_name=region)

url = s3.generate_presigned_url(
    "put_object",
    Params={
        "Bucket": bucket,
        "Key": key,
        "ContentType": "application/gpx+xml",
    },
    ExpiresIn=3600,
    HttpMethod="PUT",
)

print(url)
```

Notes:

- `HttpMethod` must be `PUT`.
- The key must exactly match the object path the device will upload.
- If `ContentType` is included during signing, the client should send the same `Content-Type` header.
- RallyBox currently sends `Content-Type: application/gpx+xml`.

## Validating The URL On A PC Before Flashing

Before changing device config, verify the presigned URL from a desktop machine.

```bash
curl -X PUT \
  -H "Content-Type: application/gpx+xml" \
  --data-binary "@test.gpx" \
  "PASTE_FULL_PRESIGNED_URL_HERE"
```

If this succeeds with HTTP `200` or `204`, the device-side upload path should be able to use the same URL format.

## About `CONFIG_RALLYBOX_GPX_S3_PRESIGNED_QUERY`

This setting is easy to misunderstand.

It only works if all of the following are true:

1. The base URL generated by firmware exactly matches the URL path that was signed.
2. The query string was generated for that exact object key.
3. The filename does not change unless a new query string is generated.

So this field is only safe for:

- One exact fixed filename
- Or a workflow that rewrites config before each upload

It is not a general-purpose solution for changing filenames.

## Splitting A Full Presigned URL Into Base URL And Query

If a generator returns a full URL like this:

```text
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=...&X-Amz-Date=...&X-Amz-Expires=3600&X-Amz-SignedHeaders=host&X-Amz-Signature=...
```

Then:

- Base URL is everything before `?`
- Presigned query string is everything after `?`

Example:

```text
Base URL:
https://amzn-s3-rallybox-bucket-911.s3.eu-central-2.amazonaws.com/tracks/rallyboxid/24042026-153000.ble.gpx

Query string:
X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=...&X-Amz-Date=...&X-Amz-Expires=3600&X-Amz-SignedHeaders=host&X-Amz-Signature=...
```

Again, that split pair is still valid only for that exact filename.

## Recommended RallyBox Approach

For RallyBox, the safest sequence is:

1. Remove device-side AWS credentials from firmware configuration.
2. Rotate any AWS credentials that were previously stored in firmware settings or source control.
3. For a quick proof test, use one full presigned URL for one known filename.
4. For normal use, add a tiny signer service that returns one fresh presigned URL per upload.

## Common Failure Modes

### `403 SignatureDoesNotMatch`

Usually means one of these:

- The filename changed after the URL was signed.
- The object key path is not identical to the signed path.
- The URL expired.
- Signed headers do not match the actual request.

### `403 AccessDenied`

Usually means one of these:

- Wrong IAM policy
- Wrong bucket or region
- Attempting to use a presigned URL for another object key

### Upload Works On PC But Fails On Device

Check:

- The device is using the exact same full URL
- The request method is `PUT`
- `Content-Type` matches what was signed, if content type was included when signing
- The device clock is not relevant for presigned URL use, but network reachability and TLS still are

## Bottom Line

If the filename changes every upload, the only clean answer is to generate a fresh presigned URL for each upload.

Static presigned query strings are not reusable authorization tokens for a filename template.