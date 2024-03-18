# How to use
# 1. Login to https://app.globus.org/settings/developers and copy a project app id and secret
# 2. Use the id and secret to create and endpoint https://funcx.readthedocs.io/en/latest/sdk.html#client-credentials-with-clients
#     $ export FUNCX_SDK_CLIENT_ID="b0500dab-ebd4-430f-b962-0c85bd43bdbb"
#     $ export FUNCX_SDK_CLIENT_SECRET="ABCDEFGHIJKLMNOP0123456789="
# 3. Set up an endpoint on the computer that will run the tests, using these instructions: https://funcx.readthedocs.io/en/latest/endpoints.html
# 4. Replace the endpoint in script to us your new endpoint

from globus_compute_sdk import Executor
import os

endpoint = '2e3dc4ed-3c37-4fe1-84c6-edc51c9f08f5'
gce = Executor(endpoint_id = endpoint)
# print("Executor : ", gce)

def run_test():
    import os
    # return os.popen("./build-test-omega_h.sh").read()
    result = os.popen("cat omega_h-test-result/LastTest.log").read()
    summary = os.popen("cat omega_h-test-result/TestSummary.log").read()
    return (summary, result)

future = gce.submit(run_test)
os.popen("mkdir -p omega_h-test-result").read()
with open("omega_h-test-result/LastTest.log", "w") as text_file:
    text_file.write("%s" % future.result()[0])
with open("omega_h-test-result/TestSummary.log", "w") as text_file:
    text_file.write("%s" % future.result()[1])
