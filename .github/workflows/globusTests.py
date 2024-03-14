from globus_compute_sdk import Executor
import os

endpoint = '7688fa75-1d82-4e10-95c1-27dcb3724e6a'
gce = Executor(endpoint_id = endpoint)

def run_test():
    import os
    # return os.popen("./build-test-omega_h.sh").read()
    result = os.popen("cat omega_h-test-result/LastTest.log").read()
    summary = os.popen("cat omega_h-test-result/TestSummary.log").read()
    return (summary, result)

# print(run_test()[0])
future = gce.submit(run_test)
os.popen("mkdir -p omega_h-test-result").read()
with open("omega_h-test-result/LastTest.log", "w") as text_file:
    text_file.write("%s" % future.result()[0])
with open("omega_h-test-result/TestSummary.log", "w") as text_file:
    text_file.write("%s" % future.result()[1])
# print("Submit returned: ", future.result()[0])
