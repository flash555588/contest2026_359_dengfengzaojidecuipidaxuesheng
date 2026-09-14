/* SPDX-License-Identifier: Apache-2.0 */
/* Application.mk renames main to espclaw_main. Use a distinct worker name. */
int claw_command_main(int argc, char **argv);
int main(int argc, char **argv)
{
    return claw_command_main(argc, argv);
}
