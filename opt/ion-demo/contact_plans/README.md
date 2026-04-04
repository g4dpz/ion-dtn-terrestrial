# Contact Plans

Contact plans define when nodes can communicate. Currently configured as continuous contacts in `ionrc` files.

## Current Setup

Both nodes use a 2-hour continuous contact window:
```
a contact +1 +7200 1 2 10000
a range +1 +7200 1 2 30
```

## Future Use

This directory is reserved for scheduled contact plan files for testing store-and-forward with link outages.
