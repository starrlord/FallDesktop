# Fall Desktop's Microsoft Store identity, from Partner Center:
# Apps and games > Fall Desktop > Product management > Product identity.
# Copy the values exactly (they are case-sensitive) and keep them ASCII. The
# DisplayName "Fall Desktop" in AppxManifest.xml (three places) must match a
# name reserved for this app in Partner Center.
# Until Name and Publisher are filled in, pack.ps1 uses a local test identity,
# which the Store rejects.
@{
    Name                 = 'darkskies.ai.FallDesktop'                      # Package/Identity/Name
    Publisher            = 'CN=9C88050D-018E-439F-A584-3F4669F53F09'      # Package/Identity/Publisher
    PublisherDisplayName = 'darkskies.ai'                                  # Package/Properties/PublisherDisplayName
}
