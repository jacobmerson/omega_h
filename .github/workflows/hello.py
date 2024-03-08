from globus_compute_sdk import Executor
tutorial_endpoint = 'edd9be03-6910-44e7-9cbc-a75794106ac3' # Public tutorial endpoint
gce = Executor(endpoint_id = tutorial_endpoint)
print("Executor : ", gce)