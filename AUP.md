# Acceptable Use Policy (AUP)

This Acceptable Use Policy (“AUP”) describes the responsible-use expectations for Kaminowaku and related project materials distributed under the BSD 3-Clause License (the “Software”).

The purpose of this AUP is to discourage unlawful, abusive, reckless, or disruptive scanning activity while preserving the Software’s role as a legitimate research, measurement, education, and authorized security tool.

This AUP is not a substitute for legal advice. Users are solely responsible for understanding and complying with the laws, contracts, rules of engagement, and authorization boundaries that apply to their use of the Software.

1. Scope
--------
This AUP applies to any individual or organization that installs, uses, distributes, modifies, or operates the Software, including personal, research, commercial, academic, governmental, enterprise, managed-service, and ISP-related use.

2. General Conduct
------------------
You are expected to use the Software responsibly and lawfully.

You may not use the Software:

  - To violate any applicable law, regulation, contract, or third-party right;
  - To gain or attempt to gain unauthorized access to systems, services, accounts, networks, or data;
  - To bypass authentication, authorization, rate limits, access controls, monitoring controls, or defensive controls on systems you do not own or operate;
  - To intentionally damage, impair, disrupt, degrade, or deny service to any person, system, service, or network;
  - To conceal abusive activity, misrepresent authorization, or evade abuse handling.

3. Active Network Scanning
--------------------------
“Active Network Scanning” includes, but is not limited to, port scanning, host discovery, banner grabbing, protocol fingerprinting, packet probing, service enumeration, routing or reachability measurement, or similar activity directed at live systems or networks.

Active Network Scanning is acceptable only when it is conducted within an authorized scope.

Authorization may include, but is not limited to:

  - Systems or networks you own or operate;
  - Systems or networks administered by your employer within the scope of your duties;
  - Systems or networks covered by a written contract, rules of engagement, statement of work, managed-service agreement, ISP agreement, security assessment authorization, bug bounty policy, research agreement, or other auditable permission;
  - Lab, training, or research environments specifically designated for such activity.

You are responsible for ensuring that scan scope, timing, rate, packet type, and collection behavior remain within the authorization you have.

4. Mass Scanning
----------------
“Mass Scanning” means automated scanning, probing, enumeration, or measurement activity that targets, or is reasonably expected to affect, a large number of unique IP addresses, hosts, services, subscribers, customers, tenants, or network endpoints.

For this AUP, Mass Scanning includes any activity that targets or may affect more than 10,000 unique IP addresses or hosts within any 24-hour period.

Mass Scanning is prohibited unless it is conducted under an authorized operational, contractual, research, enterprise, governmental, academic, or ISP-related scope.

Examples of potentially authorized Mass Scanning include:

  - An ISP scanning address space it owns, operates, routes, manages, or is contractually authorized to assess;
  - A managed security provider scanning customer networks under an active agreement;
  - An enterprise scanning its own infrastructure, subsidiaries, cloud assets, or authorized third-party environments;
  - A researcher conducting measurement work under documented authorization, institutional approval, contractual permission, or a clearly defined legal basis;
  - A government, defense, or regulated-sector operator conducting scanning within its lawful authority and approved mission scope.

Mass Scanning against networks you do not own, operate, manage, or have authorization to assess is not acceptable under this AUP.

5. Operational Safeguards
-------------------------
Users conducting Active Network Scanning or Mass Scanning are expected to implement reasonable safeguards appropriate to the size, sensitivity, and risk of the activity.

Such safeguards may include:

  - Defined scan scope before execution;
  - Rate limits, concurrency caps, retry limits, timeout controls, and backoff behavior;
  - Avoidance of fragile, safety-critical, medical, industrial-control, emergency-service, or otherwise sensitive systems unless expressly authorized;
  - Monitoring for errors, abnormal responses, packet loss, service instability, or abuse complaints;
  - Prompt suspension or reduction of scanning activity when impact is reported or reasonably suspected;
  - Logging sufficient to reconstruct what was scanned, when, by whom, and under what authorization.

6. Authorization Records
------------------------
Users conducting Mass Scanning, customer scanning, ISP-scale scanning, enterprise scanning, or third-party scanning should maintain auditable records of authorization.

Such records may include:

  - Contract, statement of work, rules of engagement, service agreement, or customer authorization;
  - Approved IP ranges, domains, ASNs, accounts, environments, ports, protocols, scan types, and time windows;
  - Expected daily or hourly scan rate;
  - Responsible operator or team;
  - Abuse/contact handling process;
  - Data retention and deletion policy.

Lack of documentation may not automatically mean a scan is unlawful, but the user bears all risk associated with being unable to prove authorization.

7. Authorized High-Volume Research and Measurement
--------------------------------------------------
This section covers high-volume research, measurement, security, or operational scanning conducted under authorized conditions, including ISP contracts, enterprise agreements, academic or institutional approval, customer-authorized assessments, government-authorized work, or other auditable authorization.

High-volume research or measurement activity is considered consistent with this AUP only when all of the following conditions are met:

  a. Authorized Scope

     The activity is limited to networks, systems, customers, subscribers, tenants, assets, or address space that the user owns, operates, manages, routes, administers, or is expressly authorized to assess.

     For third-party environments, authorization should be auditable. Examples include a signed contract, rules of engagement, customer approval, ISP agreement, institutional approval, ticketed change record, legal mandate, or other durable record showing permission.

  b. Defined Measurement Plan

     The operator documents the intended scope before scanning. The plan should identify, as applicable:

       - IP ranges, domains, ASNs, environments, or customer groups;
       - Ports, protocols, packet types, or services to be tested;
       - Expected scan rate, concurrency, schedule, and duration;
       - Data to be collected;
       - Responsible party and contact process;
       - Criteria for pausing, reducing, or terminating the scan.

  c. Operational Controls

     The operator uses reasonable controls to minimize impact, including rate limiting, concurrency limits, retry limits, timeout controls, deduplication, error handling, and backoff behavior.

     The operator should avoid unsafe scan behavior against sensitive systems unless those systems are specifically included in the authorization.

  d. Transparency and Abuse Handling

     Where feasible, the operator provides a reachable contact address, abuse mailbox, notice page, customer notification, ticket reference, or other mechanism by which affected parties can identify and report concerns about the activity.

     The operator should respond promptly and in good faith to credible abuse reports, customer concerns, or operational-impact reports.

  e. Data Minimization and Protection

     The operator collects only data reasonably necessary for the authorized purpose.

     Collected data should be protected against unauthorized access, retained only as long as needed, and deleted, anonymized, or aggregated according to the operator’s documented retention policy.

  f. Compliance With Other Rules

     Authorized high-volume research remains subject to all applicable laws, contracts, customer agreements, rules of engagement, regulatory obligations, and this AUP.

This section does not create permission to scan third-party systems without authorization. It only describes the conditions under which high-volume scanning, research, or measurement is considered acceptable under this AUP.

8. Prohibited Use
-----------------
The following uses are not acceptable:

  - Unauthorized scanning of third-party networks;
  - Unauthorized Mass Scanning of the public internet or networks outside the user’s approved scope;
  - Use intended to disrupt, degrade, or deny service;
  - Use intended to identify targets for compromise, exploitation, credential attacks, malware delivery, persistence, lateral movement, or data theft;
  - Use that ignores known abuse reports, opt-out requests, customer restrictions, or operational-impact reports without documented justification;
  - Use that intentionally falsifies authorization, origin, contact information, or responsible party information.

9. Project Enforcement
----------------------
Because the Software is distributed under the BSD 3-Clause License, this AUP does not replace or rewrite the BSD 3-Clause License.

However, violation of this AUP may result in project-level or community-level action, including:

  - Refusal of support from maintainers;
  - Removal from official project forums, issue trackers, repositories, communities, or communication channels;
  - Refusal to accept contributions;
  - Revocation of access to project-controlled infrastructure, hosted services, private repositories, documentation portals, or distribution channels;
  - Public clarification that reported activity is not endorsed by the project;
  - Referral to appropriate authorities where required or appropriate.

The maintainers reserve the right to take any action permitted by law to protect the project, users, contributors, maintainers, networks, and affected third parties.

10. No Modification of BSD 3-Clause License
-------------------------------------------
The Software is licensed under the BSD 3-Clause License.

This AUP is intended to state responsible-use expectations and project-support conditions. It does not impose additional copyright license restrictions on the rights granted by the BSD 3-Clause License unless expressly incorporated into a separate agreement signed by the relevant parties.

11. Assumption of Risk
----------------------
By using the Software, you acknowledge that network scanning can create legal, operational, contractual, and technical risk.

You are solely responsible for:

  - Obtaining appropriate authorization;
  - Defining and staying within scope;
  - Configuring safe scan rates and controls;
  - Handling collected data responsibly;
  - Responding to abuse reports or operational-impact reports;
  - Complying with applicable laws, contracts, regulations, and rules of engagement.

12. Disclaimer of Liability
---------------------------
To the fullest extent permitted by applicable law, the copyright holder, contributors, and maintainers disclaim liability for damages, claims, losses, costs, disruptions, or legal consequences arising out of or relating to use or misuse of the Software.

This includes, but is not limited to, direct, indirect, incidental, consequential, special, punitive, operational, business, reputational, contractual, or regulatory damages.

13. Acceptance
--------------
Use of the Software indicates that you have read this AUP and understand the responsible-use expectations described above.

If you do not agree with this AUP, do not request support from the maintainers, do not participate in official project spaces, and do not use project-controlled infrastructure or services.


===============================
Last updated: [July 07, 2026]
===============================
