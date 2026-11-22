# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Maintain only the derivative's translations; upstream catalogs keep their existing entries."""
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('radio_i18n', ROOT / 'tools/i18n.py')
i18n = importlib.util.module_from_spec(spec)
spec.loader.exec_module(i18n)
# key suffix | Spanish | French | Italian | Chinese
ROWS = '''ACTION_CHECKIN|Registro / OK|Pointage / OK|Presenza / OK|报到 / 正常
ACTION_SOS|SOS - enviar|SOS - envoyer|SOS - invia|SOS - 发送
ACTION_MEDICAL|Ayuda médica|Aide médicale|Aiuto medico|需要医疗援助
ACTION_HELP|Necesito ayuda|Besoin d'aide|Serve assistenza|需要援助
ACTION_RTB|Regreso a base|Retour à la base|Rientro alla base|返回基地
ACTION_ARRIVED|He llegado|Arrivé|Arrivato|已到达
ACTION_CLEAR|Fin de SOS - enviar|Fin SOS - envoyer|Fine SOS - invia|解除 SOS - 发送
ACTION_SITREP|Informe de estado|Rapport de situation|Rapporto situazione|状态报告
ACTION_CONTACT|Informar observación|Signaler observation|Segnala osservazione|报告观察
ACTION_WATER|Pedir agua|Demander eau|Richiedi acqua|请求饮用水
ACTION_FOOD|Pedir comida|Demander vivres|Richiedi cibo|请求食物
ACTION_FUEL|Pedir combustible|Demander carburant|Richiedi carburante|请求燃料
ACTION_SUPPLIES|Pedir material médico|Demander matériel médical|Richiedi materiale medico|请求医疗物资
ACTION_TRANSPORT|Pedir transporte|Demander transport|Richiedi trasporto|请求运输
ACTION_EQUIPMENT|Pedir equipo|Demander équipement|Richiedi attrezzatura|请求装备
ACTION_TASK_ACK|Tarea recibida|Tâche reçue|Compito ricevuto|任务已收到
ACTION_TASK_DONE|Tarea completada|Tâche terminée|Compito completato|任务已完成
ACTION_MESSAGE|Responder: recibido|Répondre : reçu|Risposta: ricevuto|回复：已收到
ACTION_HAZARD|Informar peligro|Signaler danger|Segnala pericolo|报告危险
AUTO|Registros automáticos|Pointages automatiques|Presenze automatiche|自动报到
INTERVAL|Intervalo de informes|Intervalle des rapports|Intervallo rapporti|报告间隔
PATH|Enviar por|Envoyer via|Invia tramite|发送方式
MOVEMENT|Informar movimiento|Rapports de mouvement|Rapporti movimento|移动报告
INBOX|Informes recibidos|Rapports reçus|Rapporti ricevuti|收到的报告
APRS_OPTIONS|Opciones APRS|Options APRS|Opzioni APRS|APRS 选项
WRITE_MESSAGE|Escribir mensaje|Écrire un message|Scrivi messaggio|编写消息
REPORT_QUEUED|Informe en cola|Rapport en attente|Rapporto in coda|报告已排队
WAIT_SEND|Enviando un informe|Un rapport est en cours|Invio rapporto in corso|报告正在发送
SOS_ACTIVE|SOS ACTIVO|SOS ACTIF|SOS ATTIVO|SOS 已激活
GPS_READY|GPS listo|GPS prêt|GPS pronto|GPS 已就绪
GPS_WAIT|Esperando GPS|En attente du GPS|Attesa GPS|等待 GPS
READY|Listo|Prêt|Pronto|就绪
QUEUED|En cola|En attente|In coda|已排队
TRANSMITTED|Transmitido|Transmis|Trasmesso|已发送
IMPORTED|Recibido en XTOC/XCOM|Reçu par XTOC/XCOM|Ricevuto da XTOC/XCOM|XTOC/XCOM 已收到
FAILED|Error - reintentar|Échec - réessayer|Invio fallito - riprova|发送失败 - 重试
TIME_WAIT|Esperando hora GPS|Attente de l'heure GPS|Attesa ora GPS|等待 GPS 时间
POSITION_WAIT|Esperando posición|En attente de position|Attesa posizione|等待位置
SETUP|Completar en XTOC/XCOM|Configurer dans XTOC/XCOM|Configura in XTOC/XCOM|在 XTOC/XCOM 完成设置
SEND_HINT|OK envía  Mantener edita|OK envoie  Maintenir modifie|OK invia  Tieni modifica|OK 发送  长按编辑
EDIT_NOTE|Texto / nota|Texte / note|Testo / nota|文本 / 备注
EDIT_QUANTITY|Cantidad / número|Quantité / nombre|Quantità / numero|数量 / 计数
EDIT_PRIORITY|Prioridad|Priorité|Priorità|优先级
EDIT_SEND|Enviar informe|Envoyer rapport|Invia rapporto|发送报告
PRI_FLASH|FLASH|FLASH|FLASH|最高紧急
PRI_IMMEDIATE|Inmediato|Immédiat|Immediata|立即
PRI_PRIORITY|Prioridad|Priorité|Priorità|优先
PRI_ROUTINE|Rutina|Routine|Ordinaria|常规
EDIT_TITLE|Editar informe|Modifier rapport|Modifica rapporto|编辑报告
EDIT_HINT|OK cambia  Atrás|OK modifie  Retour|OK cambia  Indietro|OK 更改  返回
NO_REPORTS|Sin informes recibidos|Aucun rapport reçu|Nessun rapporto ricevuto|尚未收到报告'''


def main():
    translations = dict((row[0], row[1:]) for row in (line.split('|') for line in ROWS.splitlines()))
    source, _ = i18n.extract()
    for column, code in enumerate(('es', 'fr', 'it', 'zh')):
        path = ROOT / 'core/ui/lang' / (code + '.po')
        headers, entries = i18n.read_po(path)
        # Preserve the bundled CJK face: new glyph acquisition is a separate font update. English is readable
        # for the handful of new reports outside that face; the rest of the existing Chinese UI stays Chinese.
        english_zh = {'ACTION_MEDICAL', 'ACTION_HELP', 'ACTION_CONTACT', 'ACTION_WATER', 'ACTION_FOOD', 'ACTION_FUEL',
                      'ACTION_SUPPLIES', 'ACTION_EQUIPMENT', 'ACTION_HAZARD', 'SOS_ACTIVE', 'EDIT_PRIORITY', 'PRI_PRIORITY', 'PRI_ROUTINE'}
        changed = [(key, msgid, msgid if code == 'zh' and key[7:] in english_zh else translations[key[7:]][column], False) if key and key.startswith('XSUITE_') and key[7:] in translations else (key, msgid, msgstr, fuzzy)
                   for key, msgid, msgstr, fuzzy in entries]
        i18n.write_po(path, headers, changed, {key: refs for key, _, refs in source})


if __name__ == '__main__':
    main()
