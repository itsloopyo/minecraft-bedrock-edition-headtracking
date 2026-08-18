"""Report a change signature for the published Minecraft for Windows package.

Bedrock is a Microsoft Store title, so the SteamCMD buildid poll the rest of
this catalogue uses does not apply, and the Store does not publish a build
number the way Steam does. What it does publish, per SKU, is when the listing
last changed and how large the package is. Both move when a new package ships.

That is a coarser signal than a Steam buildid and it is deliberately the
sensitive direction: it can fire on a listing change that did not touch the
EXE, and it will not stay quiet through one that did. A false positive costs
one `pixi run check-fingerprint`, which is step 1 of the issue checklist. A
missed patch costs every user their head tracking with no warning.

The real backstop is not this script. It is builds::SelectProfile, which
refuses to hook a build it does not recognise. This only buys us the chance to
have a release ready before users hit that.

Prints one line, `<lastUpdateDate>/<downloadBytes>`, and exits 1 on anything it
cannot parse - a silent failure here looks exactly like "no patch yet", which
is the single thing a patch watcher must never confuse.
"""

import argparse
import json
import sys
import urllib.request

# "Minecraft for Windows" in the Microsoft Store.
STORE_PRODUCT_ID = "9NBLGGH2JHXJ"
PACKAGE_FAMILY = "Microsoft.MinecraftUWP_8wekyb3d8bbwe"

DISPLAY_CATALOG = (
    "https://displaycatalog.mp.microsoft.com/v7.0/products/{product}"
    "?market=US&languages=en-us&fieldsTemplate=Details"
)


class Failure(Exception):
    pass


def fetch(product_id):
    url = DISPLAY_CATALOG.format(product=product_id)
    request = urllib.request.Request(url, headers={"User-Agent": "cameraunlock-patch-watch"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def signature(catalog, family):
    """(lastUpdateDate, downloadBytes) for the SKU shipping `family`.

    Several SKUs carry the same package - retail, trial, and a Game Pass entry
    among them - so the newest date and the largest size across all of them are
    taken rather than trusting whichever happens to be listed first.
    """
    product = catalog.get("Product")
    if not product:
        raise Failure("display catalog returned no Product")

    dates, sizes = [], []
    for availability in product.get("DisplaySkuAvailabilities", []):
        sku = availability.get("Sku", {})
        properties = sku.get("Properties", {})
        for package in properties.get("Packages", []):
            if package.get("PackageFamilyName") != family:
                continue
            updated = properties.get("LastUpdateDate")
            size = package.get("MaxDownloadSizeInBytes")
            if updated:
                dates.append(updated)
            if isinstance(size, int) and size > 0:
                sizes.append(size)

    if not dates or not sizes:
        raise Failure(f"no SKU in the catalog ships {family}")
    return max(dates), max(sizes)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", default=STORE_PRODUCT_ID)
    ap.add_argument("--family", default=PACKAGE_FAMILY)
    opts = ap.parse_args()

    try:
        updated, size = signature(fetch(opts.product), opts.family)
    except Failure as failure:
        print(f"::error::{failure}", file=sys.stderr)
        return 1
    except Exception as error:  # noqa: BLE001 - the caller only needs "could not answer"
        print(f"::error::querying the Microsoft Store failed: {error}", file=sys.stderr)
        return 1

    print(f"product  {opts.product} ({opts.family})", file=sys.stderr)
    print(f"updated  {updated}", file=sys.stderr)
    print(f"download {size} bytes", file=sys.stderr)
    print(f"{updated}/{size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
