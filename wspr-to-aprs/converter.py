import json
import socket
from typing import Optional, Tuple
import os

import bs4
from urllib.request import urlopen
from math import radians, cos, sin, asin, sqrt, atan2, degrees
from datetime import datetime, time, timezone
import time as time_module
import re

def main():

    #Load the APRS-IS credentials and flights from file config.json
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "config.json")





    while True:

        #Read in the config file each time we cycle through the list
        with open(config_path, "r") as config_file:
            config = json.load(config_file)
            aprsCallsign = config.get("aprsCallsign", "N0CALL")
            aprsPasscode = config.get("aprsPasscode", "12345")
            cycleDelay = config.get("cycleDelay", 120)
            flights = config.get("Flights", [])

        #Search for each of the flights in the config file
        for flight in flights:

            #Extract the information about this flight from the config array
            callsign = flight.get("callsign", "N0CALL")
            callsignGated = flight.get("callsignGated", "N0CALL")
            band = flight.get("band", "10")
            lastHeard = datetime.strptime(flight.get("lastHeard"), '%Y-%m-%d %H:%M:%S')
            lastLocation = flight.get("lastLocation", "")
            telemetry = flight.get("telemetry", "traveler")

            #Go check to see if we have a new spot for this callsign, that's newer than the lastHeard
            spot = getSpot(callsign, band, lastHeard, lastLocation)

            #See if there was a newer spot found
            if spot:
                print("New spot found:")

                #Extract the components to rebuild an APRS packet
                aprsHHMMSS = spot['Datetime'].strftime("%H%M%S")
                #aprsCallsign = spot['Callsign'].replace("/", "-")

                wsprAltitudeCoarse = calcCoarseAltitude(spot['dBm'], telemetry)

                #convert meters to feet for APRS
                wsprAltitudeCoarse = int(wsprAltitudeCoarse * 3.28084)
                # Put leading zeros on altitude
                aprsAltitudeCoarse = f"{wsprAltitudeCoarse:06d}"

                (lat, lon) = convertGridToLatLon(spot['Gridsquare'])
                latlon = f"{formatLat(lat)}/{formatLon(lon)}"

                aprsSymbol = 'O' #APRS Balloon Symbol
                aprsCourse = '000' #APRS Course
                aprsSpeed = '000'  #APRS Speed
                status = f"WSPR by {spot['ReportedBy']} on {spot['Frequency']}MHz SNR {spot['SNR']} at {spot['Datetime'].strftime('%H%M')}" 

                messageaprs = (f"{callsignGated}>APRS,TCPIP*:@{aprsHHMMSS}h{latlon}{aprsSymbol}{aprsCourse}/{aprsSpeed}/A={aprsAltitudeCoarse} {status}\r\n").encode('utf8')
                messageaprs = messageaprs.decode('utf8')        #Convert to bytes for APRS-IS

                # print("Spot Details:")
                # print(aprsHHMMSS)
                # print(callsignGated)
                # print(wsprAltitudeCoarse)
                # print(latlon)
                # print(status)

                print("APRS Message:")
                print(messageaprs)

                print("Sending to APRS-IS!")
                sendToAPRSIS(aprsCallsign, aprsPasscode, messageaprs)
           
                #Write the updated lastHeard time and lastLocation back out to the config file
                with open(config_path, "w") as config_file:
                    # Convert datetime objects back to strings for JSON serialization
                    flight["lastHeard"] = spot['Datetime'].strftime("%Y-%m-%d %H:%M:%S")
                    flight["lastLocation"] = spot['Gridsquare']
                    flight["lastAltitude"] = wsprAltitudeCoarse
                    json.dump(config, config_file, indent=4)

        #Delay x seconds before checking again
        print()

        #Print a table of all of the stations being tracked, when they were last heard, and their last known location
        print("Current Flight Status:")
        print(f"{'Callsign':<10} {'Last Heard':<20} {'Location':<10} {'Alt':<8} {'Relative Time'}")
        for flight in flights:

            utcNow = datetime.now(timezone.utc)
            utcLastHeard = datetime.strptime(flight.get("lastHeard", "1970-01-01 00:00:00"), '%Y-%m-%d %H:%M:%S').replace(tzinfo=timezone.utc)

            relativeTime = getFriendlyRelativeTime(utcNow, utcLastHeard)  
            callsign = flight.get("callsign", "N0CALL")
            lastHeard = flight.get("lastHeard", "N/A")
            lastLocation = flight.get("lastLocation", "N/A")
            altitude = flight.get("lastAltitude", "N/A")
            print(f"{callsign:<10} {utcLastHeard.strftime('%Y-%m-%d %H:%M:%S'):<20} {lastLocation:<10} {altitude:<8} ({relativeTime})")

        print(f"Waiting {cycleDelay} seconds before checking for new spots...")
        time_module.sleep(cycleDelay)        


def getFriendlyRelativeTime(dtA, dtB):
    """
    Get a friendly relative time string between two datetime objects.
    
    :param dtA: The first datetime object
    :param dtB: The second datetime object
    :return: A string representing the relative time (e.g., "5 minutes ago")
    """
    delta = dtA - dtB
    seconds = int(delta.total_seconds())
    
    if seconds < 60:
        return f"{seconds} seconds ago"
    elif seconds < 3600:
        minutes = seconds // 60
        return f"{minutes} minutes ago"
    elif seconds < 86400:
        hours = seconds // 3600
        return f"{hours} hours ago"
    else:
        days = seconds // 86400
        return f"{days} days ago"
    

def getSpot(callsign, band, lastSpotTime, lastSpotGrid):
    """
    Get the most recent WSPR spots for a given callsign from WSPRnet.
    
    :param callsign: The callsign to search for
    :param lastSpotTime: The last time a spot was recorded
    :param lastSpotGrid: The last grid square recorded
    :return: A list of spots as dictionaries
    """
    print()
    print(f"Searching for {callsign} on band {band}")
    try:
        url = "http://wsprnet.org/olddb?mode=html&band="+band+"&limit=50&findcall="+callsign+"&findreporter=&sort=date"
        page = urlopen(url)
        print(url)
        #read the source from the URL
        readHtml = page.read()
        page.close()
    except:
        print("--> Error reading WSPRnet database")
        pass
    
    try:
        #passing HTML to scrape it
        soup = bs4.BeautifulSoup(readHtml, 'html.parser')
    except:
        print("--> Error parsing HTML from WSPRnet")
        return None
    

    if lastSpotGrid is not None:
        lastSpotGrid = ""

    try:
        # Convert HTML table to array (3rd table on the page)
        spots = []
        headers = ['Datetime', 'Callsign', 'Frequency', 'SNR', 'Drift', 'Gridsquare', 
                'dBm', 'Watts', 'ReportedBy', 'ReportedByGridsquare', 'DistanceKM', 
                'DistanceMiles', 'Version']
        
        tables = soup.find_all('table')
        if len(tables) >= 3:
            table = tables[2]  # Get the 3rd table (index 2)
            for row in table.find_all('tr')[1:]:  # Skip header row
                cols = [td.get_text(strip=True) for td in row.find_all('td')]
                if cols:  # Only add rows with data
                    spot_dict = dict(zip(headers, cols))
                    # Convert Datetime string to datetime object
                    if 'Datetime' in spot_dict and spot_dict['Datetime']:
                        try:
                            spot_dict['Datetime'] = datetime.strptime(spot_dict['Datetime'], '%Y-%m-%d %H:%M')
                        except ValueError:
                            spot_dict['Datetime'] = None
                    spots.append(spot_dict)
        
        print(f"  Found {len(spots)} spots")
    except:
        print("--> Error extracting spots from HTML")
        return None

    try:
        #Loop through the spots in reverse order (oldest to newest) and find any spot newer than lastSpotTime
        for spot in reversed(spots):
            #Check if lastSpotTime is not None and if the spot is newer than lastSpotTime
            if (spot['Datetime'] is not None and lastSpotTime is not None):
                if spot['Datetime'] > lastSpotTime:
                    print("Newer spot found:")
                    if spot['Gridsquare'] != lastSpotGrid:
                        print("  Newer spot found with different grid square:")

                        if (spot['Gridsquare'] != "JJ00aa"):
                            print("  ...found a grid square that wasn't 0,0, so returning this spot")
                            print(spot)
                            return spot
    except:
        print("--> Error processing spots")
        return None
    
    #We didn't find any new spots
    print("  No new spots found.")
    return None 


class AprsISLoginError(Exception):
    pass

def sendToAPRSIS(callsign: str, passcode: str, packet: str, server: str = "rotate.aprs.net", port: int = 14580,
    app_name: str = "ptTracker", app_version: str = "1.0", filter_expr: Optional[str] = None, timeout: float = 10.0,
) -> Tuple[bool, str]:
    """
    Connect to an APRS-IS server, log in, and inject a single APRS packet.

    Parameters
    ----------
    callsign : str
        Your callsign with optional SSID, e.g., "N0CALL-9"
    passcode : str
        APRS-IS passcode for your callsign (required for verified transmit)
    packet : str
        Full APRS packet line, e.g., "N0CALL-9>APRS,TCPIP*:>hello"
    server : str
        APRS-IS server hostname (Tier-2 regional rotate recommended)
    port : int
        APRS-IS port (14580 is typical user-defined filter port)
    app_name/app_version : str
        Identification in the login "vers" field
    filter_expr : str | None
        Optional server-side filter expression appended to login line
    timeout : float
        Socket timeout in seconds

    Returns
    -------
    (verified, server_banner) : (bool, str)
        verified=True if server indicates "verified" login for your callsign.

    Notes
    -----
    - Ensures CR/LF injection is not possible in login or packet line.
    - Uses TCP_NODELAY to reduce delays (recommended for APRS-IS clients).
    """
    # Basic sanitation to avoid CR/LF injection into the APRS-IS stream
    def _clean(s: str) -> str:
        return s.replace("\r", "").replace("\n", "").strip()

    callsign = _clean(callsign)
    passcode = _clean(passcode)
    packet = _clean(packet)

    if not callsign:
        raise ValueError("callsign is required")
    if not packet:
        raise ValueError("packet is required")
    if ">" not in packet:
        raise ValueError("packet must look like an APRS frame, e.g. 'CALL>APRS,...:payload'")

    # APRS-IS login format: "user CALL pass PASS vers APP VER [filter ...]"
    # (per APRS-IS connection documentation)
    login = f"user {callsign} pass {passcode} vers {app_name} {app_version}"
    if filter_expr:
        login += f" filter { _clean(filter_expr) }"
    login += "\r\n"

    print(login)

    # APRS packets are sent as single lines terminated by CRLF
    packet_line = packet + "\r\n"


    banner = ""
    verified = False

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)

    # Reduce latency (APRS-IS recommends disabling Nagle for bidirectional clients)
    try:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        # Not fatal on all platforms
        pass

    try:
        s.connect((server, port))

        # Read initial server banner (often begins with '#')
        try:
            banner = s.recv(4096).decode("latin-1", errors="replace")
            print(banner)
        except socket.timeout:
            banner = ""
            print("No banner received")

        # Send login
        print("Sending login...")
        s.sendall(login.encode("ascii", errors="ignore"))

        # Read login response lines briefly; look for logresp with verified/unverified
        resp = ""
        try:
            resp = s.recv(4096).decode("latin-1", errors="replace")
        except socket.timeout:
            resp = ""

        # Some servers include "# logresp CALLSIGN verified" or "unverified"
        text = (banner + "\n" + resp).lower()

        if "logresp" in text and callsign.lower() in text:
            if "verified" in text and "unverified" not in text:
                verified = True
            elif "unverified" in text:
                verified = False

        # If we can detect unverified explicitly, fail fast (most servers won't gate your packets)
        if "unverified" in text:
            print("APRS-IS login unverified")
            raise AprsISLoginError(
                "APRS-IS login reported UNVERIFIED. Check callsign/passcode and APRS-IS policy."
            )

        # Send the APRS packet line
        print("Sending APRS packet...")
        print(packet_line)
        s.sendall(packet_line.encode("latin-1", errors="replace"))

        return verified, banner.strip()

    finally:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        s.close()




def convertGridToLatLon(grid):
    """
    Convert a 6-character Maidenhead grid locator (e.g., 'EM48st') to
    (latitude, longitude) in decimal degrees.

    Parameters
    ----------
    grid : str
        6-character Maidenhead locator: [A-R][A-R][0-9][0-9][A-X][A-X]
        Case-insensitive.

    Returns
    -------
    (lat, lon) : tuple[float, float]
        Latitude and longitude in decimal degrees.

    Raises
    ------
    ValueError
        If the locator is not exactly 6 characters or has invalid characters.
    """
    if not isinstance(grid, str):
        raise ValueError("Grid locator must be a string.")

    g = grid.strip()
    if len(g) != 6:
        raise ValueError("Grid locator must be exactly 6 characters (e.g., 'EM48st').")

    # Validate format (case-insensitive); subsquares are A-X or a-x
    if not re.fullmatch(r"[A-Ra-r]{2}[0-9]{2}[A-Xa-x]{2}", g):
        raise ValueError("Invalid Maidenhead locator format. Example valid: 'EM48st'.")

    g = g.upper()

    # --- Decode ---
    # Field letters
    lon_field = ord(g[0]) - ord('A')  # 0..17
    lat_field = ord(g[1]) - ord('A')  # 0..17

    # Square digits
    lon_square = int(g[2])            # 0..9
    lat_square = int(g[3])            # 0..9

    # Subsquare letters (A..X => 0..23)
    lon_sub = ord(g[4]) - ord('A')    # 0..23
    lat_sub = ord(g[5]) - ord('A')    # 0..23

    # --- Compute southwest corner in degrees ---
    lon = -180.0 + lon_field * 20.0 + lon_square * 2.0 + lon_sub * (2.0 / 24.0)
    lat =  -90.0 + lat_field * 10.0 + lat_square * 1.0 + lat_sub * (1.0 / 24.0)

    # --- Adjust to center of subsquare ---
    lon += (2.0 / 24.0) / 2.0
    lat += (1.0 / 24.0) / 2.0

    return (lat, lon)



def formatLat(lat):
    """
    Converts a latitude in decimal degrees to APRS format.

    :param lat: Latitude in decimal degrees
    """
    lat_deg = int(abs(lat))
    lat_min = (abs(lat) - lat_deg) * 60
    lat_hemisphere = 'N' if lat >= 0 else 'S'
    return f"{lat_deg:02d}{lat_min:05.2f}{lat_hemisphere}"

def formatLon(lon):
    """
    Converts a longitude in decimal degrees to APRS format.
    
    :param lon: Longitude in decimal degrees
    """
    lon_deg = int(abs(lon))
    lon_min = (abs(lon) - lon_deg) * 60
    lon_hemisphere = 'E' if lon >= 0 else 'W'
    return f"{lon_deg:03d}{lon_min:05.2f}{lon_hemisphere}"

# Calculate Coarse Altitude
def calcCoarseAltitude(power, telemetry):
    """
    Convert the power in dBm to a coarse altitude in meters.

    :param power: Power in dBm
    :param telemetry: The telemetry type, which can affect the altitude calculation
    """


    print("Calculating Coarse Altitude from power: "+str(power))
    power = int(power)  # in dBm

    match telemetry:
        case "traveler":
            match power:
                case 0: 
                    return 500
                case 3:
                    return 1500
                case 7:
                    return 2500
                case 10:
                    return 3500
                case 13:
                    return 4500
                case 17:
                    return 5500
                case 20:
                    return 6500
                case 23:
                    return 7500
                case 27:
                    return 8500
                case 30:
                    return 9500
                case 33:
                    return 10500
                case 37:
                    return 11500
                case 40:
                    return 12500
                case 43:
                    return 13500
                case 47:
                    return 14500
                case 50:
                    return 15500
                case 53:
                    return 16500
                case 57:
                    return 17500
                case 60:
                    return 18500
        case "zachtek":
            match power:
                case 0: 
                    return 450
                case 3:
                    return 1500
                case 7:
                    return 2550
                case 10:
                    return 3450
                case 13:
                    return 4500
                case 17:
                    return 5500
                case 20:
                    return 6450
                case 23:
                    return 7500
                case 27:
                    return 8550
                case 30:
                    return 9450
                case 33:
                    return 10500
                case 37:
                    return 11550
                case 40:
                    return 12450
                case 43:
                    return 13500
                case 47:
                    return 14550
                case 50:
                    return 15450
                case 53:
                    return 16500
                case 57:
                    return 17550
                case 60:
                    return 18450
    return 0


if __name__ == "__main__":
    main()