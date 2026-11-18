"""Keep the product name out of apprenticeCAM's UI data. Texts that name the program become apprenticeCAM ("About
Mastercam..."); texts about the vendor's own services (web site, registration, updates) are dropped, not rewritten."""
import re
RENAME = {'&About Mastercam...': '&About apprenticeCAM...', 'About Mastercam...': 'About apprenticeCAM...',
          'About Mastercam': 'About apprenticeCAM'}


def debrand(text):
    """-> text to keep, or None = drop the entry"""
    if text in RENAME:
        return RENAME[text]
    return None if re.search(r'mastercam|cnc software', text or '', re.I) else text
